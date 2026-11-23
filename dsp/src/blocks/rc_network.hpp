// rc_network.hpp - a linear network of resistors and capacitors, discretised
// with the trapezoidal rule.
//
// Passive tone stacks and their surroundings (source impedance, volume pot,
// bright cap, the next grid's Miller capacitance) interact: turning one knob
// moves every corner. Solving the whole network, instead of approximating it
// with separate filters, keeps that interaction exactly as the circuit has it.
//
// Each capacitor becomes its trapezoidal companion model, a conductance
// 2C/T in parallel with a history current source. Because the companion
// conductances do not depend on the signal, the node equations are solved
// once per coefficient change (when a knob moves), leaving a small matrix
// multiply per sample:
//     [capacitor voltages, output] = K * [input, history currents]
// The trapezoidal rule is the bilinear transform, so the digital response
// equals the analog response at the prewarped frequency (2/T) tan(wT/2):
// stable for any component values, no added latency.
//
// The input is an ideal voltage source behind a source resistance (the
// Thevenin equivalent of the driving tube stage). Real-time safe after
// construction: everything lives in fixed-size arrays.
#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace gigrig {

template <int Nodes, int Resistors, int Capacitors>
class RcNetwork {
public:
    static constexpr int kGround = -1;

private:
    // Array extents (std::array wants std::size_t).
    static constexpr std::size_t kN = static_cast<std::size_t>(Nodes);
    static constexpr std::size_t kR = static_cast<std::size_t>(Resistors);
    static constexpr std::size_t kC = static_cast<std::size_t>(Capacitors);

public:
    void setSampleRate(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        dirty_ = true;
    }

    // Node indices are 0 .. Nodes - 1, or kGround.
    void setResistor(int index, int a, int b, double ohms) noexcept {
        resistors_[static_cast<std::size_t>(index)] = {a, b, ohms};
        dirty_ = true;
    }

    // A capacitance of 0 removes the capacitor (an open switch); its state is cleared.
    void setCapacitor(int index, int a, int b, double farads) noexcept {
        auto& c = capacitors_[static_cast<std::size_t>(index)];
        if (!(farads > 0.0)) {
            history_[static_cast<std::size_t>(index)] = 0.0;
        }
        c = {a, b, farads};
        dirty_ = true;
    }

    void setSource(int node, double ohms) noexcept {
        sourceNode_ = node;
        sourceOhms_ = ohms;
        dirty_ = true;
    }

    void setOutput(int node) noexcept {
        outputNode_ = node;
        dirty_ = true;
    }

    void reset() noexcept { history_.fill(0.0); }

    // Solves the node equations for the current component values. Real-time safe
    // (about Nodes^3 / 3 + Nodes^2 * (Capacitors + 1) operations), so it can run
    // whenever a knob moves.
    void update() noexcept {
        if (!dirty_) {
            return;
        }
        dirty_ = false;
        constexpr int cols = Capacitors + 1;
        Matrix<kN> y{};
        std::array<std::array<double, kC + 1>, kN> rhs{};
        for (const auto& r : resistors_) {
            stamp(y, r.a, r.b, 1.0 / r.value);
        }
        stamp(y, sourceNode_, kGround, 1.0 / sourceOhms_);
        rhs[static_cast<std::size_t>(sourceNode_)][0] = 1.0 / sourceOhms_;
        for (int j = 0; j < Capacitors; ++j) {
            const auto& c = capacitors_[static_cast<std::size_t>(j)];
            const double g = 2.0 * c.value * sampleRate_;
            companion_[static_cast<std::size_t>(j)] = g;
            stamp(y, c.a, c.b, g);
            if (c.a != kGround) rhs[static_cast<std::size_t>(c.a)][static_cast<std::size_t>(j + 1)] += 1.0;
            if (c.b != kGround) rhs[static_cast<std::size_t>(c.b)][static_cast<std::size_t>(j + 1)] -= 1.0;
        }
        solveInPlace(y, rhs);  // rhs now holds node voltages per unit of each input
        for (int j = 0; j < Capacitors; ++j) {
            const auto& c = capacitors_[static_cast<std::size_t>(j)];
            for (int k = 0; k < cols; ++k) {
                k_[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] =
                    nodeValue(rhs, c.a, k) - nodeValue(rhs, c.b, k);
            }
        }
        for (int k = 0; k < cols; ++k) {
            k_[Capacitors][static_cast<std::size_t>(k)] = nodeValue(rhs, outputNode_, k);
        }
    }

    // One sample. update() must have run after the last component change.
    double process(double input) noexcept {
        std::array<double, kC> v{};
        double out = k_[Capacitors][0] * input;
        for (int k = 0; k < Capacitors; ++k) {
            out += k_[Capacitors][static_cast<std::size_t>(k + 1)] * history_[static_cast<std::size_t>(k)];
        }
        for (int j = 0; j < Capacitors; ++j) {
            const auto& row = k_[static_cast<std::size_t>(j)];
            double vj = row[0] * input;
            for (int k = 0; k < Capacitors; ++k) {
                vj += row[static_cast<std::size_t>(k + 1)] * history_[static_cast<std::size_t>(k)];
            }
            v[static_cast<std::size_t>(j)] = vj;
        }
        for (int j = 0; j < Capacitors; ++j) {
            const auto idx = static_cast<std::size_t>(j);
            history_[idx] = 2.0 * companion_[idx] * v[idx] - history_[idx];
        }
        return out;
    }

    // Analog response v(output) / v(source) at a frequency, from the same
    // component values. Used by tests and for checking designs; not for the audio path.
    std::complex<double> analogResponse(double frequencyHz) const noexcept {
        using C = std::complex<double>;
        const C s(0.0, 2.0 * 3.14159265358979323846 * frequencyHz);
        std::array<std::array<C, kN + 1>, kN> m{};
        auto add = [&m](int a, int b, C g) {
            if (a != kGround) m[static_cast<std::size_t>(a)][static_cast<std::size_t>(a)] += g;
            if (b != kGround) m[static_cast<std::size_t>(b)][static_cast<std::size_t>(b)] += g;
            if (a != kGround && b != kGround) {
                m[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] -= g;
                m[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] -= g;
            }
        };
        for (const auto& r : resistors_) add(r.a, r.b, C(1.0 / r.value));
        for (const auto& c : capacitors_) add(c.a, c.b, s * c.value);
        add(sourceNode_, kGround, C(1.0 / sourceOhms_));
        m[static_cast<std::size_t>(sourceNode_)][Nodes] = 1.0 / sourceOhms_;
        // Gaussian elimination with partial pivoting on the augmented matrix.
        for (int col = 0; col < Nodes; ++col) {
            int pivot = col;
            for (int r = col + 1; r < Nodes; ++r) {
                if (std::abs(m[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) >
                    std::abs(m[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)])) {
                    pivot = r;
                }
            }
            std::swap(m[static_cast<std::size_t>(col)], m[static_cast<std::size_t>(pivot)]);
            const auto& prow = m[static_cast<std::size_t>(col)];
            for (int r = col + 1; r < Nodes; ++r) {
                auto& row = m[static_cast<std::size_t>(r)];
                const C f = row[static_cast<std::size_t>(col)] / prow[static_cast<std::size_t>(col)];
                for (int k = col; k <= Nodes; ++k)
                    row[static_cast<std::size_t>(k)] -= f * prow[static_cast<std::size_t>(k)];
            }
        }
        std::array<C, kN> v{};
        for (int r = Nodes - 1; r >= 0; --r) {
            const auto& row = m[static_cast<std::size_t>(r)];
            C sum = row[Nodes];
            for (int k = r + 1; k < Nodes; ++k)
                sum -= row[static_cast<std::size_t>(k)] * v[static_cast<std::size_t>(k)];
            v[static_cast<std::size_t>(r)] = sum / row[static_cast<std::size_t>(r)];
        }
        return outputNode_ == kGround ? C(0.0) : v[static_cast<std::size_t>(outputNode_)];
    }

private:
    template <std::size_t N>
    using Matrix = std::array<std::array<double, N>, N>;

    struct Element {
        int a = kGround;
        int b = kGround;
        double value = 1.0;
    };

    static void stamp(Matrix<kN>& y, int a, int b, double g) noexcept {
        if (a != kGround) y[static_cast<std::size_t>(a)][static_cast<std::size_t>(a)] += g;
        if (b != kGround) y[static_cast<std::size_t>(b)][static_cast<std::size_t>(b)] += g;
        if (a != kGround && b != kGround) {
            y[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] -= g;
            y[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] -= g;
        }
    }

    template <std::size_t Cols>
    static double nodeValue(const std::array<std::array<double, Cols>, kN>& x, int node, int col) noexcept {
        return node == kGround ? 0.0 : x[static_cast<std::size_t>(node)][static_cast<std::size_t>(col)];
    }

    // Gaussian elimination with partial pivoting; the node matrix of a
    // connected RC network with a grounded source is non-singular.
    template <std::size_t Cols>
    static void solveInPlace(Matrix<kN>& a, std::array<std::array<double, Cols>, kN>& b) noexcept {
        for (int col = 0; col < Nodes; ++col) {
            const auto c = static_cast<std::size_t>(col);
            std::size_t pivot = c;
            for (std::size_t r = c + 1; r < Nodes; ++r) {
                if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
            }
            std::swap(a[c], a[pivot]);
            std::swap(b[c], b[pivot]);
            for (std::size_t r = c + 1; r < Nodes; ++r) {
                const double f = a[r][c] / a[c][c];
                if (f == 0.0) continue;
                for (std::size_t k = c; k < Nodes; ++k) a[r][k] -= f * a[c][k];
                for (std::size_t k = 0; k < Cols; ++k) b[r][k] -= f * b[c][k];
            }
        }
        for (int r = Nodes - 1; r >= 0; --r) {
            const auto ri = static_cast<std::size_t>(r);
            for (std::size_t k = 0; k < Cols; ++k) {
                double sum = b[ri][k];
                for (std::size_t j = ri + 1; j < Nodes; ++j) sum -= a[ri][j] * b[j][k];
                b[ri][k] = sum / a[ri][ri];
            }
        }
    }

    std::array<Element, kR> resistors_{};
    std::array<Element, kC> capacitors_{};
    std::array<double, kC> history_{};
    std::array<double, kC> companion_{};
    std::array<std::array<double, kC + 1>, kC + 1> k_{};
    double sampleRate_ = 48000.0;
    double sourceOhms_ = 1.0;
    int sourceNode_ = 0;
    int outputNode_ = 0;
    bool dirty_ = true;
};

}  // namespace gigrig

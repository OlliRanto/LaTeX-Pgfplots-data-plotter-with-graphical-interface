#pragma once
#include <string>
#include <vector>

// Noise removal methods for the Data editor. Plain C++ (no Qt) so it can be tested on its own.
//
// Every method works on ONE column y(x). Rows whose x or y is not a finite number are ignored and stay empty in the
// result; the remaining points are processed along increasing x, whatever the row order is. Methods that need evenly
// spaced samples (Butterworth, FFT) resample onto an even grid internally and interpolate back, so uneven data is fine.
// Methods that work "per point" (Moving average, Gaussian, Median, Hampel, Bilateral, Wiener, Wavelet, Whittaker,
// Total variation, Kalman, Exponential) use the point number as the distance, i.e. they assume roughly even spacing.
// Savitzky-Golay and LOWESS use the real x values.
namespace nf {

enum class Method {
    MovingAverage,     // window, weights (uniform / triangular / Hann / Hamming / Blackman), edge
    Gaussian,          // sigma (points), edge
    Exponential,       // alpha, direction (forward / backward / zero-lag)
    Holt,              // double exponential smoothing (level + trend): alpha, beta
    SavitzkyGolay,     // window, polynomial order
    Median,            // window, edge
    Hampel,            // window, n sigmas, edge          (spike / outlier removal with a rolling median)
    Outlier,           // global outlier removal: z-score / modified z-score (MAD) / IQR, threshold, replacement
    Lowess,            // fraction of points, degree, robust iterations
    Whittaker,         // Whittaker-Eilers smoother: lambda, difference order
    TotalVariation,    // total variation (ROF) denoising: lambda, iterations
    Bilateral,         // edge preserving: window, sigma space (points), sigma range (y units, 0 = automatic)
    Wiener,            // adaptive local Wiener filter: window, noise variance (0 = automatic)
    Kalman,            // local-level Kalman filter / RTS smoother: process-to-measurement noise ratio
    Butterworth,       // low / high / band pass / band stop, order, cut-off(s), zero-phase
    Fft,               // FFT filter: low / high / band pass / band stop / keep strongest components
    Wavelet,           // wavelet shrinkage: family, levels, threshold rule, soft / hard
};

enum class Edge { Mirror, Nearest, Shrink };          // how windows behave at the ends of the data
enum class Window { Uniform, Triangular, Hann, Hamming, Blackman };
enum class Direction { Forward, Backward, ZeroLag };
enum class OutlierRule { ZScore, ModifiedZ, Iqr };
enum class Replace { Interpolate, Nan, Clip };
enum class PassType { LowPass, HighPass, BandPass, BandStop };
enum class FftMode { LowPass, HighPass, BandPass, BandStop, KeepStrongest };
enum class Threshold { Soft, Hard };
enum class ThresholdRule { Universal, Manual };
enum class CutoffUnit { FractionOfNyquist, Frequency };   // Frequency = cycles per X unit

struct Params {
    Method method = Method::MovingAverage;

    // window based methods (odd number of points; even numbers are made odd)
    int window = 5;
    Edge edge = Edge::Mirror;
    Window weights = Window::Uniform;      // Moving average

    double sigma = 2.0;                    // Gaussian: standard deviation in points
    double alpha = 0.3;                    // Exponential, Holt: 0 < alpha <= 1
    double beta = 0.1;                     // Holt trend smoothing
    Direction direction = Direction::ZeroLag;

    int polyOrder = 3;                     // Savitzky-Golay, LOWESS degree (0..2)
    double fraction = 0.1;                 // LOWESS: share of the points in every local fit
    int robustIterations = 2;              // LOWESS

    double nSigmas = 3.0;                  // Hampel: threshold in (MAD based) standard deviations
    OutlierRule outlierRule = OutlierRule::ModifiedZ;
    double threshold = 3.5;                // Outlier: z / modified z / IQR factor
    Replace replace = Replace::Interpolate;

    double lambda = 100.0;                 // Whittaker: smoothness;  Total variation: strength
    int diffOrder = 2;                     // Whittaker
    int iterations = 500;                  // Total variation

    double sigmaSpace = 3.0;               // Bilateral
    double sigmaRange = 0.0;               // Bilateral: 0 = estimate from the data
    double noiseVariance = 0.0;            // Wiener: 0 = estimate from the data

    double qRatio = 0.05;                  // Kalman: process noise / measurement noise
    bool smoother = true;                  // Kalman: also run the backward (RTS) pass -> no lag

    PassType pass = PassType::LowPass;     // Butterworth
    int order = 4;                         // Butterworth order
    bool zeroPhase = true;                 // Butterworth: forward-backward (no phase shift, twice the steepness)
    CutoffUnit cutoffUnit = CutoffUnit::FractionOfNyquist;
    double cutoff1 = 0.1;                  // Butterworth / FFT: (lower) cut-off
    double cutoff2 = 0.3;                  // Butterworth / FFT: upper cut-off of band pass / band stop
    FftMode fftMode = FftMode::LowPass;
    double keepPercent = 10.0;             // FFT KeepStrongest: keep components above this % of the strongest one

    int waveletIndex = 3;                  // index into waveletNames()
    int levels = 0;                        // Wavelet: 0 = as many as the length allows
    Threshold thresholdType = Threshold::Soft;
    ThresholdRule thresholdRule = ThresholdRule::Universal;
    double thresholdValue = 1.0;           // Wavelet: universal threshold multiplier, or the manual threshold
};

// Names of the wavelet families offered ("Haar", "Daubechies 2", ...).
const std::vector<std::string> &waveletNames();

// Short text for the column name, e.g. "Savitzky-Golay, window 11, order 3".
std::string describe(const Params &p);

// y(x) -> out (same length as the input, NaN where x or y is missing). Returns false and fills `error` on failure.
bool filter(const std::vector<double> &x, const std::vector<double> &y, const Params &p,
            std::vector<double> &out, std::string &error);

// Sampling info of the valid points of x (sorted): number of points and the mean spacing. Used by the dialog to show the Nyquist frequency.
struct SamplingInfo { int points = 0; double dx = 0; bool even = true; };
SamplingInfo samplingInfo(const std::vector<double> &x, const std::vector<double> &y);

} // namespace nf

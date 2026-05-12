#include "audio_io.h"
#include "vox.h"

#ifdef VOX_QT_HAVE_DONGLE
#include "dongle_client.h"
#include <QSerialPortInfo>
#endif

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QGridLayout>
#include <QLatin1Char>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kSampleRate = 8000;
constexpr int kFrameMs = 20;
constexpr int kFrameSize = (kSampleRate * kFrameMs) / 1000;
constexpr double kFullScale = 32767.0;

// ---------- Light-mode theme ----------------------------------------
//
// Single source of truth for colors used by inline stylesheets and by
// the custom widgets' QPainter calls.  When we refactor to a real
// theme system later, all named uses can flip to a different palette
// without hunting through paintEvent code.
namespace theme {
    // Surfaces
    constexpr const char *kPanelBg     = "#ffffff";
    constexpr const char *kPanelBorder = "#cbd5e1";
    constexpr const char *kInputBg     = "#f1f5f9";
    constexpr const char *kSubtleBg    = "#f8fafc";
    constexpr const char *kGridLine    = "#e2e8f0";
    constexpr const char *kAxisGuide   = "#94a3b8";

    // Text
    constexpr const char *kTextPrimary   = "#1e293b";
    constexpr const char *kTextSecondary = "#64748b";

    // LED off state
    constexpr const char *kLedOffBg   = "#e2e8f0";
    constexpr const char *kLedOffText = "#64748b";

    // PTT block
    constexpr const char *kPttOnBg    = "#dcfce7";
    constexpr const char *kPttOnText  = "#166534";
    constexpr const char *kPttOffBg   = "#fee2e2";
    constexpr const char *kPttOffText = "#991b1b";

    // Data series — chosen for distinguishability on a white background
    // (saturated mid-darks rather than pastels).
    constexpr const char *kSeriesMicRaw   = "#15803d";  // green
    constexpr const char *kSeriesMicPost  = "#c2410c";  // burnt orange
    constexpr const char *kSeriesRx       = "#1d4ed8";  // blue
    constexpr const char *kSeriesSnr      = "#6d28d9";  // purple
    constexpr const char *kSeriesVadRaw   = "#fb923c";  // light orange
    constexpr const char *kSeriesVadValid = "#ea580c";  // orange
    constexpr const char *kSeriesThresh   = "#f97316";  // orange (dashed ref)
    constexpr const char *kSeriesPtt      = "#22c55e";  // green band

    // DualMicBar: low-marker strip and the AEC-attenuation strip
    constexpr const char *kMicBarLow   = "#86efac";  // mint
    constexpr const char *kMicBarHigh  = "#fda4af";  // rose
    constexpr const char *kMicBarLineRaw  = "#0f172a";
    constexpr const char *kMicBarLinePost = "#a16207";

    // Residual-delay polyline + peak marker
    constexpr const char *kResidualLine = "#0284c7";
    constexpr const char *kResidualPeak = "#d97706";
}  // namespace theme

QProgressBar *createBar(int minValue, int maxValue)
{
    auto *bar = new QProgressBar();
    bar->setRange(minValue, maxValue);
    bar->setValue(0);
    bar->setTextVisible(true);
    return bar;
}

double levelToDbfs(int level)
{
    if (level <= 0)
        return -90.0;

    double normalized = static_cast<double>(level) / kFullScale;
    if (normalized < 1.0e-9)
        normalized = 1.0e-9;
    return 20.0 * std::log10(normalized);
}

double ratioToDb(int numerator, int denominator)
{
    if (numerator <= 0 || denominator <= 0)
        return 0.0;
    return 20.0 * std::log10(static_cast<double>(numerator) / static_cast<double>(denominator));
}

QString formatDbfs(int level)
{
    double value = levelToDbfs(level);
    return QString("%1%2 dBFS")
        .arg(value >= 0.0 ? "+" : "")
        .arg(value, 5, 'f', 1, QLatin1Char(' '));
}

QString formatDb(double value)
{
    return QString("%1%2 dB")
        .arg(value >= 0.0 ? "+" : "")
        .arg(value, 5, 'f', 1, QLatin1Char(' '));
}

QString formatDbfsTenths(int dbfsTenths)
{
    double value = (double)dbfsTenths / 10.0;
    return QString("%1%2 dBFS")
        .arg(value >= 0.0 ? "+" : "")
        .arg(value, 5, 'f', 1, QLatin1Char(' '));
}

QString formatSignedInt(int value, int width)
{
    return QString("%1").arg(value, width, 10, QLatin1Char(' '));
}

QLabel *createValueLabel()
{
    auto *label = new QLabel("0");
    label->setMinimumWidth(84);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setStyleSheet(QString(
        "background:%1;"
        "color:%2;"
        "border:1px solid %3;"
        "border-radius:5px;"
        "padding:4px 8px;"
        "font-family:monospace;")
        .arg(theme::kInputBg, theme::kTextPrimary, theme::kPanelBorder));
    return label;
}

void addMetricRow(QGridLayout *layout, int row, int column, const QString &name, QLabel **valueOut)
{
    auto *nameLabel = new QLabel(name);
    auto *valueLabel = createValueLabel();
    layout->addWidget(nameLabel, row, column);
    layout->addWidget(valueLabel, row, column + 1);
    *valueOut = valueLabel;
}

QLabel *createLedLamp(const QString &name)
{
    auto *lamp = new QLabel(name);
    lamp->setMinimumWidth(72);
    lamp->setAlignment(Qt::AlignCenter);
    lamp->setStyleSheet(QString(
        "background:%1;"
        "color:%2;"
        "border:1px solid %3;"
        "border-radius:10px;"
        "padding:4px;"
        "font-weight:600;")
        .arg(theme::kLedOffBg, theme::kLedOffText, theme::kPanelBorder));
    return lamp;
}

void setLedLamp(QLabel *lamp, bool on, const QString &onColor)
{
    if (!lamp)
        return;

    if (on) {
        lamp->setStyleSheet(QString(
            "background:%1;"
            "color:#ffffff;"
            "border:1px solid #1f2937;"
            "border-radius:10px;"
            "padding:4px;"
            "font-weight:700;").arg(onColor));
    } else {
        lamp->setStyleSheet(QString(
            "background:%1;"
            "color:%2;"
            "border:1px solid %3;"
            "border-radius:10px;"
            "padding:4px;"
            "font-weight:600;")
            .arg(theme::kLedOffBg, theme::kLedOffText, theme::kPanelBorder));
    }
}

QWidget *createSliderRow(const QString &labelText, int minValue, int maxValue,
                         int initialValue, QSlider **sliderOut, QLabel **valueOut)
{
    auto *row = new QWidget();
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *label = new QLabel(labelText);
    auto *slider = new QSlider(Qt::Horizontal);
    auto *value = new QLabel(QString::number(initialValue));

    slider->setRange(minValue, maxValue);
    slider->setValue(initialValue);
    value->setMinimumWidth(50);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(label);
    layout->addWidget(slider, 1);
    layout->addWidget(value);

    QObject::connect(slider, &QSlider::valueChanged, value,
                     [value](int v) { value->setText(QString::number(v)); });

    *sliderOut = slider;
    *valueOut = value;
    return row;
}

class DualMicBar : public QWidget {
public:
    explicit DualMicBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(24);
    }

    void setLevels(int rawLevel, int postLevel, const QString &rawText, const QString &postText)
    {
        raw_ = std::clamp(rawLevel, 0, max_);
        post_ = std::clamp(postLevel, 0, max_);
        rawText_ = rawText;
        postText_ = postText;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        (void)event;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect r = rect().adjusted(1, 1, -1, -1);
        const int radius = 5;

        p.setPen(QPen(QColor(theme::kPanelBorder), 1));
        p.setBrush(QColor(theme::kInputBg));
        p.drawRoundedRect(r, radius, radius);

        const int w = std::max(1, r.width());
        const int x0 = r.left();
        const int y0 = r.top();
        const int h = r.height();

        const int rawX = x0 + (raw_ * w) / max_;
        const int postX = x0 + (post_ * w) / max_;
        const int lowX = std::min(rawX, postX);
        const int highX = std::max(rawX, postX);

        if (lowX > x0)
            p.fillRect(QRect(x0, y0, lowX - x0, h), QColor(theme::kMicBarLow));
        if (highX > lowX)
            p.fillRect(QRect(lowX, y0, highX - lowX, h), QColor(theme::kMicBarHigh));

        p.setPen(QPen(QColor(theme::kMicBarLineRaw), 2));
        p.drawLine(rawX, y0, rawX, y0 + h - 1);
        p.setPen(QPen(QColor(theme::kMicBarLinePost), 2));
        p.drawLine(postX, y0, postX, y0 + h - 1);

        const QString text = QString("M:%1  P:%2").arg(rawText_).arg(postText_);
        p.setPen(QColor(theme::kTextPrimary));
        p.drawText(r, Qt::AlignCenter, text);
    }

private:
    int raw_ = 0;
    int post_ = 0;
    int max_ = 32767;
    QString rawText_ = "+0.0 dBFS";
    QString postText_ = "+0.0 dBFS";
};

class ResidualDelayPlot : public QWidget {
public:
    explicit ResidualDelayPlot(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(130);
        profile_.fill(0);
    }

    void setProfile(const VoxDebugState &debug)
    {
        for (int i = 0; i < VOX_RESIDUAL_CORR_PROFILE_POINTS; i++)
            profile_[i] = std::clamp(debug.residual_corr_profile[i], 0, 100);
        minDelaySamples_ = debug.residual_corr_delay_min_samples;
        stepSamples_ = debug.residual_corr_delay_step_samples;
        peakDelaySamples_ = debug.residual_corr_peak_delay_samples;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        (void)event;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect r = rect().adjusted(1, 1, -1, -1);
        p.setPen(QPen(QColor(theme::kPanelBorder), 1));
        p.setBrush(QColor(theme::kPanelBg));
        p.drawRoundedRect(r, 6, 6);

        const QRect plot = r.adjusted(8, 8, -8, -22);
        if (plot.width() < 10 || plot.height() < 10)
            return;

        const int zeroIdx = (0 - minDelaySamples_) / std::max(1, stepSamples_);
        if (zeroIdx >= 0 && zeroIdx < VOX_RESIDUAL_CORR_PROFILE_POINTS) {
            const int zx = plot.left() + (zeroIdx * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            p.setPen(QPen(QColor(theme::kAxisGuide), 1, Qt::DashLine));
            p.drawLine(zx, plot.top(), zx, plot.bottom());
        }

        QPolygon poly;
        poly.reserve(VOX_RESIDUAL_CORR_PROFILE_POINTS);
        for (int i = 0; i < VOX_RESIDUAL_CORR_PROFILE_POINTS; i++) {
            int x = plot.left() + (i * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            int y = plot.bottom() - (profile_[i] * plot.height()) / 100;
            poly << QPoint(x, y);
        }

        p.setPen(QPen(QColor(theme::kResidualLine), 2));
        p.drawPolyline(poly);

        int peakIdx = (peakDelaySamples_ - minDelaySamples_) / std::max(1, stepSamples_);
        if (peakIdx >= 0 && peakIdx < VOX_RESIDUAL_CORR_PROFILE_POINTS) {
            int px = plot.left() + (peakIdx * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            int py = plot.bottom() - (profile_[peakIdx] * plot.height()) / 100;
            p.setPen(QPen(QColor(theme::kResidualPeak), 2));
            p.setBrush(QColor(theme::kResidualPeak));
            p.drawEllipse(QPoint(px, py), 3, 3);
        }

        p.setPen(QColor(theme::kTextSecondary));
        p.drawText(QRect(plot.left(), plot.bottom() + 2, plot.width(), 16),
                   Qt::AlignLeft | Qt::AlignVCenter, "delay-");
        p.drawText(QRect(plot.left(), plot.bottom() + 2, plot.width(), 16),
                   Qt::AlignRight | Qt::AlignVCenter, "delay+");
    }

private:
    std::array<int, VOX_RESIDUAL_CORR_PROFILE_POINTS> profile_{};
    int minDelaySamples_ = VOX_RESIDUAL_CORR_DELAY_MIN_SAMPLES;
    int stepSamples_ = VOX_RESIDUAL_CORR_DELAY_STEP_SAMPLES;
    int peakDelaySamples_ = 0;
};

/*
 * TimeSeriesPlot — scrolling time-series view of VOX state.
 *
 * Four stacked panels share one time axis (latest sample on the right):
 *   1) Levels (dBFS, -90..0)        : mic_raw, mic_post_aec, rx
 *   2) SNR    (dB,    -10..+30)     : post-AEC SNR vs noise floor
 *   3) VAD    (%,       0..100)     : raw + validated, with the active
 *                                      VAD threshold drawn as a dashed line
 *   4) PTT    (boolean band)        : filled green when on
 *
 * Storage is a single ring buffer of HistoryFrame.  Pushing a sample is
 * O(1); painting is one walk over the buffer, so cost scales linearly
 * with capacity.  Default 30 s at 50 fps = 1500 frames ≈ 48 KB total —
 * trivial.
 *
 * Units mirror the existing scalar widgets (dBFS / dB / %) so a glance
 * at the right edge matches the bars and labels above.
 */
struct HistoryFrame {
    float mic_raw_dbfs;
    float mic_post_dbfs;
    float rx_dbfs;
    float snr_db;
    int   vad_raw_pct;
    int   vad_validated_pct;
    int   vad_threshold_pct;
    bool  ptt;
};

class TimeSeriesPlot : public QWidget {
public:
    static constexpr int kHistorySeconds = 30;
    static constexpr int kFps             = 50;
    static constexpr int kCapacity        = kHistorySeconds * kFps;

    explicit TimeSeriesPlot(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(280);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
        history_.fill(HistoryFrame{-90.0f, -90.0f, -90.0f, 0.0f, 0, 0, 60, false});
    }

    void pushSample(const HistoryFrame &f)
    {
        history_[head_] = f;
        head_ = (head_ + 1) % kCapacity;
        if (filled_ < kCapacity)
            filled_++;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        (void)event;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect r = rect().adjusted(1, 1, -1, -1);
        p.setPen(QPen(QColor(theme::kPanelBorder), 1));
        p.setBrush(QColor(theme::kPanelBg));
        p.drawRoundedRect(r, 6, 6);

        // Layout: 4 panels stacked, with thin separators.
        // Allocate vertical space proportionally:
        //   levels: 0.36  snr: 0.22  vad: 0.30  ptt: 0.12
        const QRect inner = r.adjusted(8, 6, -8, -6);
        const int total_h = inner.height();
        const int gap     = 4;
        const int h_ptt   = 18;
        const int h_remaining = total_h - h_ptt - 3 * gap;
        const int h_lvl   = (h_remaining * 36) / 88;
        const int h_snr   = (h_remaining * 22) / 88;
        const int h_vad   = h_remaining - h_lvl - h_snr;

        QRect r_lvl = QRect(inner.left(), inner.top(),                    inner.width(), h_lvl);
        QRect r_snr = QRect(inner.left(), r_lvl.bottom() + gap,           inner.width(), h_snr);
        QRect r_vad = QRect(inner.left(), r_snr.bottom() + gap,           inner.width(), h_vad);
        QRect r_ptt = QRect(inner.left(), r_vad.bottom() + gap,           inner.width(), h_ptt);

        drawPanelBg(p, r_lvl, "Levels (dBFS)");
        drawPanelBg(p, r_snr, "SNR (dB)");
        drawPanelBg(p, r_vad, "VAD (%)");
        drawPanelBg(p, r_ptt, "PTT");

        if (filled_ == 0)
            return;

        // Levels panel: -90 dBFS at bottom, 0 dBFS at top.
        drawHGrid(p, r_lvl, /*minv=*/-90.0f, /*maxv=*/0.0f, /*step=*/30.0f);
        drawSeriesF(p, r_lvl, -90.0f, 0.0f,
                    [](const HistoryFrame &f){ return f.mic_raw_dbfs; },  QColor(theme::kSeriesMicRaw));
        drawSeriesF(p, r_lvl, -90.0f, 0.0f,
                    [](const HistoryFrame &f){ return f.mic_post_dbfs; }, QColor(theme::kSeriesMicPost));
        drawSeriesF(p, r_lvl, -90.0f, 0.0f,
                    [](const HistoryFrame &f){ return f.rx_dbfs; },       QColor(theme::kSeriesRx));
        drawLegend(p, r_lvl, {{"mic",  QColor(theme::kSeriesMicRaw)},
                              {"post", QColor(theme::kSeriesMicPost)},
                              {"rx",   QColor(theme::kSeriesRx)}});

        // SNR panel: -10 dB at bottom, +30 dB at top, single line.
        drawHGrid(p, r_snr, -10.0f, 30.0f, 10.0f);
        drawSeriesF(p, r_snr, -10.0f, 30.0f,
                    [](const HistoryFrame &f){ return f.snr_db; }, QColor(theme::kSeriesSnr));
        drawLegend(p, r_snr, {{"snr", QColor(theme::kSeriesSnr)}});

        // VAD panel: 0..100 % with active threshold as a dashed reference line.
        drawHGrid(p, r_vad, 0.0f, 100.0f, 25.0f);
        // Threshold line (uses the most recent sample's threshold as a single
        // horizontal line; if it changes mid-window we'd need a separate
        // series, but threshold tweaking is a slow human action).
        const HistoryFrame &latest = history_[(head_ + kCapacity - 1) % kCapacity];
        drawHorizontalRefLine(p, r_vad, 0.0f, 100.0f,
                              (float)latest.vad_threshold_pct,
                              QColor(theme::kSeriesThresh), Qt::DashLine);
        drawSeriesI(p, r_vad, 0, 100,
                    [](const HistoryFrame &f){ return f.vad_raw_pct; },       QColor(theme::kSeriesVadRaw));
        drawSeriesI(p, r_vad, 0, 100,
                    [](const HistoryFrame &f){ return f.vad_validated_pct; }, QColor(theme::kSeriesVadValid));
        drawLegend(p, r_vad, {{"raw",   QColor(theme::kSeriesVadRaw)},
                              {"valid", QColor(theme::kSeriesVadValid)},
                              {"thr",   QColor(theme::kSeriesThresh)}});

        // PTT panel: filled rectangles where ptt was on.
        drawPttBand(p, r_ptt);
    }

private:
    void drawPanelBg(QPainter &p, const QRect &r, const QString &title)
    {
        p.setPen(QPen(QColor(theme::kPanelBorder), 1));
        p.setBrush(QColor(theme::kSubtleBg));
        p.drawRoundedRect(r, 4, 4);

        p.setPen(QColor(theme::kTextSecondary));
        p.setFont(QFont("monospace", 8));
        p.drawText(r.adjusted(4, 1, -4, -1), Qt::AlignLeft | Qt::AlignTop, title);
    }

    void drawHGrid(QPainter &p, const QRect &r, float minv, float maxv, float step)
    {
        if (maxv <= minv || step <= 0)
            return;
        p.setPen(QPen(QColor(theme::kGridLine), 1, Qt::DashLine));
        for (float v = minv; v <= maxv + 1e-3f; v += step) {
            float t = (v - minv) / (maxv - minv);
            int y = r.bottom() - int(t * r.height());
            p.drawLine(r.left(), y, r.right(), y);
        }
    }

    void drawHorizontalRefLine(QPainter &p, const QRect &r, float minv, float maxv,
                               float v, QColor color, Qt::PenStyle style)
    {
        if (maxv <= minv) return;
        float t = (v - minv) / (maxv - minv);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int y = r.bottom() - int(t * r.height());
        p.setPen(QPen(color, 1, style));
        p.drawLine(r.left(), y, r.right(), y);
    }

    template<typename Getter>
    void drawSeriesF(QPainter &p, const QRect &r, float minv, float maxv,
                     Getter getter, QColor color)
    {
        if (filled_ == 0 || maxv <= minv) return;

        QPolygon poly;
        poly.reserve(filled_);
        for (int i = 0; i < filled_; i++) {
            // Map oldest sample to the LEFT edge, newest to the RIGHT edge.
            int sample_idx = (head_ + kCapacity - filled_ + i) % kCapacity;
            float v = getter(history_[sample_idx]);
            if (v < minv) v = minv;
            if (v > maxv) v = maxv;
            float t  = (v - minv) / (maxv - minv);
            int   x  = r.left() + (i * r.width()) / std::max(1, kCapacity - 1);
            int   y  = r.bottom() - int(t * r.height());
            poly << QPoint(x, y);
        }
        p.setPen(QPen(color, 1.5));
        p.drawPolyline(poly);
    }

    template<typename Getter>
    void drawSeriesI(QPainter &p, const QRect &r, int minv, int maxv,
                     Getter getter, QColor color)
    {
        // Wrap the int getter into a float getter so we don't duplicate the body.
        drawSeriesF(p, r, (float)minv, (float)maxv,
                    [getter](const HistoryFrame &f) { return (float)getter(f); },
                    color);
    }

    void drawPttBand(QPainter &p, const QRect &r)
    {
        if (filled_ == 0) return;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(theme::kSeriesPtt));
        // Walk the buffer; for each contiguous run of ptt==true draw a rectangle.
        int run_start_i = -1;
        for (int i = 0; i <= filled_; i++) {
            bool on = false;
            if (i < filled_) {
                int idx = (head_ + kCapacity - filled_ + i) % kCapacity;
                on = history_[idx].ptt;
            }
            if (on && run_start_i < 0) {
                run_start_i = i;
            } else if (!on && run_start_i >= 0) {
                int x0 = r.left() + (run_start_i * r.width()) / std::max(1, kCapacity - 1);
                int x1 = r.left() + (i           * r.width()) / std::max(1, kCapacity - 1);
                p.drawRect(QRect(x0, r.top() + 12, x1 - x0, r.height() - 14));
                run_start_i = -1;
            }
        }
    }

    void drawLegend(QPainter &p, const QRect &r, std::initializer_list<std::pair<QString, QColor>> items)
    {
        p.setFont(QFont("monospace", 7));
        int x = r.right() - 6;
        QFontMetrics fm(p.font());
        for (auto it = items.end(); it != items.begin(); ) {
            --it;
            int w = fm.horizontalAdvance(it->first) + 14;
            x -= w;
            p.setPen(Qt::NoPen);
            p.setBrush(it->second);
            p.drawRect(x, r.top() + 3, 8, 8);
            p.setPen(it->second);
            p.drawText(x + 12, r.top() + 11, it->first);
        }
    }

    std::array<HistoryFrame, kCapacity> history_{};
    int head_   = 0;
    int filled_ = 0;
};

class VoxWindow : public QMainWindow {
public:
    VoxWindow()
    {
        setWindowTitle("VOX Linux Test GUI");
        resize(880, 860);

        auto *root = new QWidget();
        auto *layout = new QVBoxLayout(root);

        // Device picker — at the top so "where am I getting audio from?"
        // is the first thing the user sees.  Source picker (Local /
        // Dongle), plus mic and rx for the local case, plus a Refresh
        // button (audio devices and serial ports come and go on USB
        // plug events).
        auto *deviceForm = new QFormLayout();
        sourceCombo_    = new QComboBox();
        micDeviceCombo_ = new QComboBox();
        rxDeviceCombo_  = new QComboBox();
        sourceCombo_->setMinimumWidth(360);
        micDeviceCombo_->setMinimumWidth(360);
        rxDeviceCombo_->setMinimumWidth(360);

        auto *refreshBtn = new QPushButton("Refresh");
        refreshBtn->setToolTip("Re-enumerate audio devices and dongle ports "
                               "(after USB plug/unplug)");

        auto *sourceRow = new QHBoxLayout();
        sourceRow->addWidget(sourceCombo_, 1);
        sourceRow->addSpacing(refreshBtn->sizeHint().width() + 6);

        auto *micRow = new QHBoxLayout();
        micRow->addWidget(micDeviceCombo_, 1);
        micRow->addWidget(refreshBtn);

        auto *rxRow = new QHBoxLayout();
        rxRow->addWidget(rxDeviceCombo_, 1);
        rxRow->addSpacing(refreshBtn->sizeHint().width() + 6);   /* align with mic row */

        deviceForm->addRow("Source", sourceRow);
        deviceForm->addRow("Microphone", micRow);
        deviceForm->addRow("RX (speaker monitor)", rxRow);

        /* Inject-from-PC checkbox: only meaningful while a dongle is
         * connected — when checked, mic+rx come from the host audio
         * combos and get shipped to the chip as INJECT_PCM frames in
         * place of its on-board source.  Lets the GUI drive the chip's
         * vox_process with canned / live PC audio without an analog
         * front end. */
        injectPcCheckbox_ = new QCheckBox("Inject PC audio into dongle (uses mic + rx above)");
        injectPcCheckbox_->setToolTip(
            "When connected to a dongle: capture from the selected PC mic "
            "and rx devices and stream them as INJECT_PCM frames to the "
            "chip in place of its ADC / synthetic input.");
        injectPcCheckbox_->setEnabled(false);  /* gated by source mode */
        deviceForm->addRow(QString(), injectPcCheckbox_);

        layout->addLayout(deviceForm);

        // Selection changes reopen the audio io with the new pick.
        // Use the activated() signal (vs currentIndexChanged) so we
        // ignore the initial population that runs before audio_ exists,
        // and only react to actual user picks.
        connect(sourceCombo_, QOverload<int>::of(&QComboBox::activated),
                this, [this](int){ applySourceMode(); });
        connect(micDeviceCombo_, QOverload<int>::of(&QComboBox::activated),
                this, [this](int){ reopenAudio(); });
        connect(rxDeviceCombo_,  QOverload<int>::of(&QComboBox::activated),
                this, [this](int){ reopenAudio(); });
        connect(refreshBtn, &QPushButton::clicked,
                this, [this](){ populateDeviceDropdowns(); });
        connect(injectPcCheckbox_, &QCheckBox::toggled,
                this, [this](bool on){ applyInjectMode(on); });

        auto *form = new QFormLayout();

        micLevelBar_ = new DualMicBar();
        vadRawScoreBar_ = createBar(0, 100);
        vadScoreBar_ = createBar(0, 100);
        rxLevelBar_ = createBar(0, 32767);
        aecScoreBar_ = createBar(0, 100);

        form->addRow("Microphone Raw/Post", micLevelBar_);
        form->addRow("VAD Score (Raw)", vadRawScoreBar_);
        form->addRow("VAD Score (Validated)", vadScoreBar_);
        form->addRow("Speaker Monitor Level", rxLevelBar_);
        form->addRow("AEC Score", aecScoreBar_);

        layout->addLayout(form);

        auto *historyTitle = new QLabel("History (last 30 s)");
        historyTitle->setStyleSheet("font-weight:700;");
        layout->addWidget(historyTitle);

        timeSeriesPlot_ = new TimeSeriesPlot();
        layout->addWidget(timeSeriesPlot_);

        auto *ledTitle = new QLabel("MCU LED Indicators");
        ledTitle->setStyleSheet("font-weight:700;");
        layout->addWidget(ledTitle);

        auto *ledRow = new QHBoxLayout();
        micLedLamp_ = createLedLamp("MIC");
        rxLedLamp_ = createLedLamp("RX");
        vadLedLamp_ = createLedLamp("VAD");
        aecLedLamp_ = createLedLamp("AEC");
        pttLedLamp_ = createLedLamp("PTT");
        ledRow->addWidget(micLedLamp_);
        ledRow->addWidget(rxLedLamp_);
        ledRow->addWidget(vadLedLamp_);
        ledRow->addWidget(aecLedLamp_);
        ledRow->addWidget(pttLedLamp_);
        ledRow->addStretch(1);

        layout->addLayout(ledRow);

        auto *pttRow = new QHBoxLayout();
        auto *pttLabel = new QLabel("PTT");
        pttIndicator_ = new QLabel("OFF");
        pttIndicator_->setMinimumWidth(80);
        pttIndicator_->setAlignment(Qt::AlignCenter);
        pttIndicator_->setStyleSheet(QString(
            "background:%1;color:%2;border-radius:6px;padding:6px;font-weight:600;")
            .arg(theme::kPttOffBg, theme::kPttOffText));
        pttRow->addWidget(pttLabel);
        pttRow->addWidget(pttIndicator_);
        pttRow->addStretch(1);

        layout->addLayout(pttRow);

        auto *debugTitle = new QLabel("Diagnostics");
        debugTitle->setStyleSheet("font-weight:700;");
        layout->addWidget(debugTitle);

        auto *debugGrid = new QGridLayout();
        debugGrid->setColumnStretch(0, 1);
        debugGrid->setColumnStretch(1, 0);
        debugGrid->setColumnStretch(2, 1);
        debugGrid->setColumnStretch(3, 0);

        addMetricRow(debugGrid, 0, 0, "Mic After AEC", &micPostAecValue_);
        addMetricRow(debugGrid, 0, 2, "Noise Floor", &noiseFloorValue_);
        addMetricRow(debugGrid, 1, 0, "SNR", &snrValue_);
        addMetricRow(debugGrid, 1, 2, "AEC Atten", &energyMarginValue_);
        addMetricRow(debugGrid, 2, 0, "Raw Voice", &rawVoiceValue_);
        addMetricRow(debugGrid, 2, 2, "Validated Voice", &validatedVoiceValue_);
        addMetricRow(debugGrid, 3, 0, "Hang", &hangValueDisplay_);
        addMetricRow(debugGrid, 3, 2, "RX Active", &rxActiveValue_);
        addMetricRow(debugGrid, 4, 0, "Gate VAD", &gateVadValue_);
        addMetricRow(debugGrid, 4, 2, "Gate SNR", &gateSnrValue_);
        addMetricRow(debugGrid, 5, 0, "Energy OK", &energyOkValue_);
        addMetricRow(debugGrid, 5, 2, "PTT Reason", &pttReasonValue_);
        addMetricRow(debugGrid, 6, 0, "Residual Corr", &residualCorrValue_);
        addMetricRow(debugGrid, 6, 2, "Residual Level", &residualLevelValue_);

        auto *debugPanel = new QWidget();
        debugPanel->setLayout(debugGrid);
        layout->addWidget(debugPanel);

        auto *corrTitle = new QLabel("Residual Correlation vs Delay");
        corrTitle->setStyleSheet("font-weight:700;");
        layout->addWidget(corrTitle);

        residualDelayPlot_ = new ResidualDelayPlot();
        layout->addWidget(residualDelayPlot_);

        residualPeakLabel_ = new QLabel("Peak: --");
        residualPeakLabel_->setStyleSheet(QString(
            "font-family:monospace;color:%1;").arg(theme::kTextSecondary));
        layout->addWidget(residualPeakLabel_);

        decisionSummaryLabel_ = new QLabel("Waiting for first audio frame...");
        decisionSummaryLabel_->setWordWrap(true);
        decisionSummaryLabel_->setStyleSheet(QString(
            "background:%1;"
            "color:%2;"
            "border:1px solid %3;"
            "border-radius:6px;"
            "padding:8px;"
            "font-family:monospace;")
            .arg(theme::kInputBg, theme::kTextPrimary, theme::kPanelBorder));
        layout->addWidget(decisionSummaryLabel_);

        auto *sliderTitle = new QLabel("Tuning");
        sliderTitle->setStyleSheet("font-weight:700;");
        layout->addWidget(sliderTitle);

        layout->addWidget(createSliderRow("PTT Hang (ms)", 50, 2000, 500,
                          &hangSlider_, &hangValue_));
        layout->addWidget(createSliderRow("MIC Threshold", 50, 4000, 300,
                          &micThreshSlider_, &micThreshValue_));
        layout->addWidget(createSliderRow("RX Threshold", 50, 4000, 300,
                          &rxThreshSlider_, &rxThreshValue_));
        layout->addWidget(createSliderRow("VAD Prob Threshold", 5, 100, 60,
                          &vadThreshSlider_, &vadThreshValue_));
        layout->addWidget(createSliderRow("AEC Reduction Threshold (%)", 1, 100, 20,
                          &aecThreshSlider_, &aecThreshValue_));
        layout->addWidget(createSliderRow("RX Guard VAD Boost", 0, 40, 15,
                  &rxGuardVadBoostSlider_, &rxGuardVadBoostValue_));
        layout->addWidget(createSliderRow("RX Guard SNR (%)", 110, 260, 145,
                  &rxGuardSnrSlider_, &rxGuardSnrValue_));

        /* In Local mode the sliders are read once per onTick frame into
         * a fresh VoxConfig (the existing pump).  In Dongle mode the
         * chip is the one running vox_process, so any slider change has
         * to travel down the wire — debounced into SET_TUNING. */
        for (QSlider *s : { hangSlider_, micThreshSlider_, rxThreshSlider_,
                            vadThreshSlider_, aecThreshSlider_,
                            rxGuardVadBoostSlider_, rxGuardSnrSlider_ }) {
            connect(s, &QSlider::valueChanged,
                    this, [this](int){ scheduleTuningPush(); });
        }

        statusLabel_ = new QLabel("Initializing audio...");
        layout->addWidget(statusLabel_);

        /* Dongle LOG panel — shows VOX_MSG_LOG strings as they arrive.
         * Read-only; auto-scrolls; cap at a few hundred lines so a
         * chatty chip doesn't slowly eat memory.  Hidden in local mode
         * because it'd just be dead space. */
        dongleLogPanel_ = new QPlainTextEdit();
        dongleLogPanel_->setReadOnly(true);
        dongleLogPanel_->setMaximumBlockCount(500);
        dongleLogPanel_->setMinimumHeight(80);
        dongleLogPanel_->setStyleSheet(QString(
            "background:%1;color:%2;border:1px solid %3;"
            "border-radius:6px;padding:6px;font-family:monospace;")
            .arg(theme::kInputBg, theme::kTextPrimary, theme::kPanelBorder));
        dongleLogPanel_->setPlaceholderText("Dongle LOG messages will appear here.");
        dongleLogPanel_->setVisible(false);
        layout->addWidget(dongleLogPanel_);

        setCentralWidget(root);

        // Populate the device dropdowns once before any first-time
        // audio open — the auto-load below may select a non-default
        // device, and we need that device to exist in the combo before
        // we can set it.
        populateDeviceDropdowns();

        VoxConfig vcfg = {};
        vcfg.sample_rate = kSampleRate;
        vcfg.frame_size = kFrameSize;
        vcfg.hang_ms = 500;
        vcfg.mic_led_threshold = 300;
        vcfg.rx_led_threshold = 300;
        vcfg.vad_led_prob_threshold = 60;
        vcfg.aec_led_reduction_pct = 20;
        vcfg.rx_guard_vad_boost = 15;
        vcfg.rx_guard_snr_pct = 145;

        vox_ = vox_create(&vcfg);
        if (!vox_) {
            statusLabel_->setText("VOX init failed.");
            return;
        }

        // Tuning persistence: build the File menu, then attempt to load
        // a previous session's tuning + device picks from the default
        // path.  Missing or malformed files fall back silently to the
        // compile-time defaults — first-run users see exactly what they
        // saw before this feature existed.
        buildMenuBar();
        const QString defaultPath = defaultTuningPath();
        if (QFile::exists(defaultPath))
            loadTuningFile(defaultPath);

        // Now that the device combos reflect either auto or the loaded
        // picks, open the audio with the current selection.
        reopenAudio();

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() { onTick(); });
        timer->start(kFrameMs);
    }

    ~VoxWindow() override
    {
        if (vox_)
            vox_destroy(vox_);
        if (audio_)
            audio_io_close(audio_);
    }

protected:
    /* Auto-save tuning at exit so the next launch picks up where this
     * one ended.  Honors the same default-path logic as auto-load. */
    void closeEvent(QCloseEvent *event) override
    {
        const QString path = defaultTuningPath();
        if (saveTuningFile(path)) {
            // Saved successfully; nothing to do (no UI left to update).
        }
        QMainWindow::closeEvent(event);
    }

private:
    /* ---------- Audio device picker --------------------------------- */
    /*
     * The dropdowns hold one entry per audio device, with the canonical
     * device-id string (e.g. "pulse:alsa_input.usb-...") in the item's
     * userData and a short label as the display.  An "auto" entry is
     * always at index 0.  Selection change triggers reopenAudio().
     */
    QString currentMicDeviceId() const
    {
        return micDeviceCombo_ ? micDeviceCombo_->currentData().toString() : "auto";
    }
    QString currentRxDeviceId() const
    {
        return rxDeviceCombo_ ? rxDeviceCombo_->currentData().toString() : "auto";
    }

    /* Try to select an item by its userData id; returns true on hit.
     * If the id isn't present (device disappeared), falls back to
     * "auto" (index 0) and returns false. */
    static bool selectDeviceById(QComboBox *combo, const QString &id)
    {
        if (!combo) return false;
        for (int i = 0; i < combo->count(); i++) {
            if (combo->itemData(i).toString() == id) {
                combo->setCurrentIndex(i);
                return true;
            }
        }
        combo->setCurrentIndex(0);   /* fall back to "auto" */
        return false;
    }

    void populateDeviceDropdowns()
    {
        // Snapshot what was selected so we can restore after the rebuild.
        const QString prevMic = currentMicDeviceId();
        const QString prevRx  = currentRxDeviceId();

        // Block currentIndexChanged signals during the rebuild — we
        // don't want a transient "auto"-pass through to fire reopen.
        QSignalBlocker mb(micDeviceCombo_);
        QSignalBlocker rb(rxDeviceCombo_);

        micDeviceCombo_->clear();
        rxDeviceCombo_->clear();

        // Always-present "auto" entry — matches the CLI's default and
        // is the only thing first-run users will see.
        micDeviceCombo_->addItem("auto (system default)", QString("auto"));
        rxDeviceCombo_->addItem("auto (system default)",  QString("auto"));

        // Enumerate via audio_io.  We size for plenty; pulse + alsa
        // rarely exceed a dozen on a normal machine.
        std::vector<AudioDeviceInfo> devs(64);
        const int n = audio_io_enumerate(devs.data(), (int)devs.size());
        const int filled = std::min(n, (int)devs.size());

        for (int i = 0; i < filled; i++) {
            const AudioDeviceInfo &d = devs[i];
            const QString id      = QString::fromUtf8(d.id);
            const QString display = QString::fromUtf8(d.display);
            if (d.kind & AUDIO_DEV_MIC)
                micDeviceCombo_->addItem(display, id);
            if (d.kind & AUDIO_DEV_RX)
                rxDeviceCombo_->addItem(display, id);
        }

        // Restore previous selection if still present.
        selectDeviceById(micDeviceCombo_, prevMic);
        selectDeviceById(rxDeviceCombo_,  prevRx);

        // Re-enumerate available dongle ports too — they show up under
        // the same Refresh button and on the same USB plug events.
        populateSourceCombo();

        statusBar()->showMessage(
            QString("Found %1 audio device%2.")
                .arg(filled)
                .arg(filled == 1 ? "" : "s"),
            2000);
    }

    /* Source picker: "Local PC audio" + one entry per serial port that
     * looks like a dongle.  Item data is the canonical id we read in
     * applySourceMode(): either "local" or "dongle:<systemLocation>". */
    void populateSourceCombo()
    {
        if (!sourceCombo_) return;
        const QString prev = sourceCombo_->currentData().toString();

        QSignalBlocker sb(sourceCombo_);
        sourceCombo_->clear();
        sourceCombo_->addItem("Local PC audio", QString("local"));

#ifdef VOX_QT_HAVE_DONGLE
        /* Enumerate every serial port — we don't filter by vendor/product
         * because the ST-Link VCP appears with various descriptions and
         * future chip-USB will appear under a different VID/PID again.
         * The user picks the right one; we just list candidates. */
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto &p : ports) {
            const QString sysLoc = p.systemLocation();
            QString display = QString("Dongle: %1").arg(sysLoc);
            const QString desc = p.description();
            if (!desc.isEmpty())
                display += QString("  (%1)").arg(desc);
            sourceCombo_->addItem(display, QString("dongle:%1").arg(sysLoc));
        }
#else
        /* Build without Qt SerialPort — nothing to enumerate.  Keep the
         * combo present so the layout doesn't jump; tooltip explains. */
        sourceCombo_->setToolTip("Built without Qt SerialPort: "
                                 "dongle mode unavailable.");
#endif

        // Restore previous pick if still present; otherwise "local".
        for (int i = 0; i < sourceCombo_->count(); ++i) {
            if (sourceCombo_->itemData(i).toString() == prev) {
                sourceCombo_->setCurrentIndex(i);
                return;
            }
        }
        sourceCombo_->setCurrentIndex(0);
    }

    /* React to a Source combo change.  Two modes are mutually exclusive —
     * Local pumps audio_io+vox_process here in onTick(); Dongle relies on
     * STATE_FRAME signals from the chip and disables the mic/rx combos
     * (we don't hold the host mic at all in dongle mode). */
    void applySourceMode()
    {
        const QString sel = sourceCombo_->currentData().toString();

        if (sel.startsWith("dongle:")) {
            /* Tear down local audio first so we don't fight for the
             * physical mic and so onTick stops calling vox_process. */
            if (audio_) {
                audio_io_close(audio_);
                audio_ = nullptr;
            }
            /* mic/rx combos default disabled here; the inject checkbox
             * re-enables them so the user can pick capture devices. */
            micDeviceCombo_->setEnabled(false);
            rxDeviceCombo_->setEnabled(false);
            sourceMode_        = SourceMode::Dongle;
            currentDonglePort_ = sel.mid(QStringLiteral("dongle:").size());
            if (dongleLogPanel_)   dongleLogPanel_->setVisible(true);
            if (injectPcCheckbox_) injectPcCheckbox_->setEnabled(true);

#ifdef VOX_QT_HAVE_DONGLE
            ensureDongleClient();
            if (statusLabel_)
                statusLabel_->setText(QString("Opening dongle %1...")
                                          .arg(currentDonglePort_));
            if (dongleClient_->open(currentDonglePort_)) {
                /* Make the chip authoritative for the current GUI state
                 * right at connect.  Run mode = NORMAL (chip runs its
                 * own synthetic / live audio source) so we never inherit
                 * a stale INJECT or PAUSED from a previous session.
                 * SET_TUNING after that aligns thresholds with sliders. */
                dongleClient_->sendSetMode(VOX_RUN_NORMAL);
                pushTuningToDongleIfActive();
            }
#else
            if (statusLabel_)
                statusLabel_->setText("Dongle mode unavailable in this build.");
#endif
        } else {
            /* Local mode — close the dongle if it was open, then reopen
             * audio with whatever the mic/rx combos show. */
#ifdef VOX_QT_HAVE_DONGLE
            if (dongleClient_)
                dongleClient_->close();
#endif
            /* Switching away from dongle while inject was on: just
             * flip the local flag (audio_ will be reopened below for
             * the local pump anyway).  Don't send SET_MODE — the port
             * is closing and the chip's mode is no longer our concern. */
            dongleInjectActive_ = false;
            currentDonglePort_.clear();
            sourceMode_ = SourceMode::Local;
            micDeviceCombo_->setEnabled(true);
            rxDeviceCombo_->setEnabled(true);
            if (dongleLogPanel_) dongleLogPanel_->setVisible(false);
            if (injectPcCheckbox_) {
                QSignalBlocker b(injectPcCheckbox_);
                injectPcCheckbox_->setChecked(false);
                injectPcCheckbox_->setEnabled(false);
            }
            reopenAudio();
        }
    }

    /* Enable / disable PC-audio injection while in dongle mode.  When
     * on: re-enable the mic/rx combos, open audio_io against the user's
     * picks, tell the chip SET_MODE=INJECT so it consumes our PCM.
     * When off: close audio_io, disable the combos, SET_MODE=NORMAL so
     * the chip goes back to its own source (synthetic or ADC). */
    void applyInjectMode(bool on)
    {
        if (sourceMode_ != SourceMode::Dongle) {
            /* Shouldn't happen — checkbox is gated by source mode — but
             * be defensive: silently revert the check state. */
            if (injectPcCheckbox_) {
                QSignalBlocker b(injectPcCheckbox_);
                injectPcCheckbox_->setChecked(false);
            }
            return;
        }

        if (on) {
            dongleInjectActive_ = true;
            micDeviceCombo_->setEnabled(true);
            rxDeviceCombo_->setEnabled(true);
            reopenAudio();   /* opens audio_ since gate now passes */
#ifdef VOX_QT_HAVE_DONGLE
            if (dongleClient_ && dongleClient_->isOpen())
                dongleClient_->sendSetMode(VOX_RUN_INJECT);
#endif
        } else {
            dongleInjectActive_ = false;
#ifdef VOX_QT_HAVE_DONGLE
            if (dongleClient_ && dongleClient_->isOpen())
                dongleClient_->sendSetMode(VOX_RUN_NORMAL);
#endif
            if (audio_) {
                audio_io_close(audio_);
                audio_ = nullptr;
            }
            micDeviceCombo_->setEnabled(false);
            rxDeviceCombo_->setEnabled(false);
        }
    }

    /* Slider-change hook: kick a 30 ms debounce timer.  Dragging a
     * slider fires valueChanged dozens of times per second; coalescing
     * keeps us from flooding the 921600-baud uplink with SET_TUNING
     * messages and matches the chip's frame rate (50 fps = 20 ms). */
    void scheduleTuningPush()
    {
        if (sourceMode_ != SourceMode::Dongle) return;
#ifdef VOX_QT_HAVE_DONGLE
        if (!dongleTuningPushTimer_) {
            dongleTuningPushTimer_ = new QTimer(this);
            dongleTuningPushTimer_->setSingleShot(true);
            dongleTuningPushTimer_->setInterval(30);
            connect(dongleTuningPushTimer_, &QTimer::timeout,
                    this, [this]() { pushTuningToDongleIfActive(); });
        }
        dongleTuningPushTimer_->start();
#endif
    }

    /* Build a VoxConfig from current slider values and ship it to the
     * chip via SET_TUNING.  No-op if not in dongle mode or port is
     * closed (drag a slider before connecting and nothing tries to
     * write to a non-existent serial port). */
    void pushTuningToDongleIfActive()
    {
#ifdef VOX_QT_HAVE_DONGLE
        if (sourceMode_ != SourceMode::Dongle)            return;
        if (!dongleClient_ || !dongleClient_->isOpen())   return;

        VoxConfig cfg = {};
        cfg.sample_rate              = kSampleRate;
        cfg.frame_size               = kFrameSize;
        cfg.hang_ms                  = hangSlider_->value();
        cfg.mic_led_threshold        = micThreshSlider_->value();
        cfg.rx_led_threshold         = rxThreshSlider_->value();
        cfg.vad_led_prob_threshold   = vadThreshSlider_->value();
        cfg.aec_led_reduction_pct    = aecThreshSlider_->value();
        cfg.rx_guard_vad_boost       = rxGuardVadBoostSlider_->value();
        cfg.rx_guard_snr_pct         = rxGuardSnrSlider_->value();
        dongleClient_->sendTuning(cfg);
#endif
    }

#ifdef VOX_QT_HAVE_DONGLE
    /* Lazy-construct the DongleClient on first use.  Parents it to this
     * window for Qt-ownership cleanup, wires its signals to widget
     * setters / status updates. */
    void ensureDongleClient()
    {
        if (dongleClient_) return;
        dongleClient_ = new DongleClient(this);

        connect(dongleClient_, &DongleClient::connectionChanged,
                this, [this](bool c) {
                    if (!statusLabel_) return;
                    if (c) {
                        statusLabel_->setText(
                            QString("Dongle connected: %1 (waiting for HELLO...)")
                                .arg(currentDonglePort_));
                    } else {
                        statusLabel_->setText(
                            QString("Dongle disconnected: %1")
                                .arg(currentDonglePort_));
                    }
                });

        connect(dongleClient_, &DongleClient::helloReceived,
                this, [this](const QString &rev, quint32 caps, quint32 hwId) {
                    dongleFwRevision_  = rev;
                    dongleCapabilities_ = caps;
                    dongleHwId_         = hwId;
                    if (statusLabel_) {
                        statusLabel_->setText(
                            QString("Dongle %1  fw=%2  caps=0x%3  hw=0x%4")
                                .arg(currentDonglePort_)
                                .arg(rev.isEmpty() ? QStringLiteral("?") : rev)
                                .arg(caps, 0, 16)
                                .arg(hwId, 0, 16));
                    }
                });

        connect(dongleClient_, &DongleClient::stateFrameReceived,
                this, [this](const VoxLedState &led,
                             const VoxDebugState &dbg,
                             int ptt) {
                    /* Pump the same widget-update path the local mode
                     * uses; the heartbeat counter still drives the
                     * status-bar "frames received" tick in onTick. */
                    Q_UNUSED(ptt);
                    ++dongleStateFramesSeen_;
                    renderState(led, dbg);
                });

        connect(dongleClient_, &DongleClient::logReceived,
                this, [this](const QString &line) {
                    /* Drop the trailing newline if the chip-side put one
                     * in — appendPlainText adds its own paragraph break. */
                    QString trimmed = line;
                    while (trimmed.endsWith('\n') || trimmed.endsWith('\r'))
                        trimmed.chop(1);
                    if (dongleLogPanel_)
                        dongleLogPanel_->appendPlainText(trimmed);
                    /* Mirror to stderr too — useful when running under
                     * a terminal and the panel isn't visible. */
                    qDebug().noquote() << "[dongle log]" << trimmed;
                });

        connect(dongleClient_, &DongleClient::ackReceived,
                this, [](quint8 inResponseTo, quint8 status, quint32 info) {
                    qDebug() << "[dongle ack] for_type=" << inResponseTo
                             << "status=" << status << "info=" << info;
                });
    }
#endif

    /* Tear down audio_, reopen with whatever the dropdowns currently
     * show.  Failure leaves audio_ NULL and reports via status label;
     * onTick already handles audio_ == nullptr by skipping reads.
     *
     * In dongle mode this is a no-op unless inject is active — we
     * don't want to grab the host mic at all when we're just viewing
     * the chip's state stream. */
    void reopenAudio()
    {
        if (sourceMode_ == SourceMode::Dongle && !dongleInjectActive_)
            return;

        if (audio_) {
            audio_io_close(audio_);
            audio_ = nullptr;
        }

        const QByteArray micId = currentMicDeviceId().toUtf8();
        const QByteArray rxId  = currentRxDeviceId().toUtf8();

        AudioIOConfig acfg = {};
        acfg.mic_device  = micId.constData();
        acfg.rx_device   = rxId.constData();
        acfg.sample_rate = kSampleRate;
        acfg.frame_size  = kFrameSize;

        audio_ = audio_io_open(&acfg);
        if (!audio_) {
            if (statusLabel_)
                statusLabel_->setText(
                    QString("Audio open failed for mic='%1' rx='%2'. "
                            "Check Pulse/Pipewire and try Refresh.")
                        .arg(QString::fromUtf8(micId))
                        .arg(QString::fromUtf8(rxId)));
            return;
        }
        if (statusLabel_)
            statusLabel_->setText(
                QString("Running: mic=%1  rx=%2")
                    .arg(QString::fromUtf8(micId))
                    .arg(QString::fromUtf8(rxId)));
    }

    /* ---------- Tuning persistence ---------------------------------- */
    /*
     * The tuning is the slider values (which feed vox_set_tuning each
     * frame) plus the selected mic and rx device IDs.  JSON keeps the
     * file human-editable, future-proof (a "version" field anchors
     * migrations), and easy to email between developers.  No QSettings;
     * we want a portable file we can inspect with a text editor.
     */
    static constexpr int kTuningFileVersion = 1;

    QString defaultTuningPath() const
    {
        // QStandardPaths::AppConfigLocation maps to e.g. ~/.config/<app>
        // on Linux, %APPDATA%/<app> on Windows, ~/Library/Preferences
        // on macOS.  Single source of truth for "where does our tuning
        // live by default."
        const QString dir = QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        return dir + "/tuning.json";
    }

    /* Build a JSON object snapshot of the current slider values.
     * Format is the on-disk contract — keep field names stable. */
    QJsonObject captureTuningJson() const
    {
        QJsonObject o;
        o["version"]                   = kTuningFileVersion;
        o["hang_ms"]                   = hangSlider_->value();
        o["mic_led_threshold"]         = micThreshSlider_->value();
        o["rx_led_threshold"]          = rxThreshSlider_->value();
        o["vad_led_prob_threshold"]    = vadThreshSlider_->value();
        o["aec_led_reduction_pct"]     = aecThreshSlider_->value();
        o["rx_guard_vad_boost"]        = rxGuardVadBoostSlider_->value();
        o["rx_guard_snr_pct"]          = rxGuardSnrSlider_->value();
        o["mic_device"]                = currentMicDeviceId();
        o["rx_device"]                 = currentRxDeviceId();
        return o;
    }

    /* Apply a JSON tuning object to the sliders.  Missing fields are
     * left unchanged (backward-compat: an old file with fewer keys
     * still loads; only present sliders are overwritten). */
    void applyTuningJson(const QJsonObject &o)
    {
        const int fileVersion = o.value("version").toInt(0);
        if (fileVersion > kTuningFileVersion) {
            statusBar()->showMessage(
                QString("Tuning file is newer (version %1) than this app understands; "
                        "loading what we recognize.").arg(fileVersion), 5000);
        }

        auto setIfPresent = [&](const QString &key, QSlider *slider) {
            if (slider && o.contains(key) && o[key].isDouble())
                slider->setValue(o[key].toInt());
        };

        setIfPresent("hang_ms",                   hangSlider_);
        setIfPresent("mic_led_threshold",         micThreshSlider_);
        setIfPresent("rx_led_threshold",          rxThreshSlider_);
        setIfPresent("vad_led_prob_threshold",    vadThreshSlider_);
        setIfPresent("aec_led_reduction_pct",     aecThreshSlider_);
        setIfPresent("rx_guard_vad_boost",        rxGuardVadBoostSlider_);
        setIfPresent("rx_guard_snr_pct",          rxGuardSnrSlider_);

        // Device picks.  selectDeviceById falls back to "auto" if the
        // saved device isn't present right now (e.g. unplugged USB
        // mic), so the GUI is robust to environment changes.
        if (o.contains("mic_device") && o["mic_device"].isString())
            selectDeviceById(micDeviceCombo_, o["mic_device"].toString());
        if (o.contains("rx_device") && o["rx_device"].isString())
            selectDeviceById(rxDeviceCombo_, o["rx_device"].toString());
    }

    bool saveTuningFile(const QString &path)
    {
        QJsonDocument doc(captureTuningJson());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            statusBar()->showMessage(
                QString("Tuning save failed: %1").arg(f.errorString()), 5000);
            return false;
        }
        f.write(doc.toJson(QJsonDocument::Indented));
        statusBar()->showMessage(QString("Saved tuning to %1").arg(path), 3000);
        return true;
    }

    bool loadTuningFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(
                QString("Tuning load failed: %1").arg(f.errorString()), 5000);
            return false;
        }
        QByteArray bytes = f.readAll();
        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            statusBar()->showMessage(
                QString("Tuning file invalid: %1").arg(err.errorString()), 5000);
            return false;
        }
        applyTuningJson(doc.object());
        statusBar()->showMessage(QString("Loaded tuning from %1").arg(path), 3000);
        return true;
    }

    void buildMenuBar()
    {
        QMenu *fileMenu = menuBar()->addMenu("&File");

        QAction *saveAct = fileMenu->addAction("&Save Tuning");
        saveAct->setShortcut(QKeySequence::Save);   /* Ctrl-S */
        connect(saveAct, &QAction::triggered, this, [this]() {
            saveTuningFile(defaultTuningPath());
        });

        QAction *saveAsAct = fileMenu->addAction("Save Tuning &As…");
        saveAsAct->setShortcut(QKeySequence::SaveAs); /* Ctrl-Shift-S */
        connect(saveAsAct, &QAction::triggered, this, [this]() {
            const QString path = QFileDialog::getSaveFileName(
                this, "Save Tuning As", defaultTuningPath(),
                "JSON files (*.json);;All files (*)");
            if (!path.isEmpty())
                saveTuningFile(path);
        });

        QAction *loadAct = fileMenu->addAction("&Load Tuning…");
        loadAct->setShortcut(QKeySequence::Open);   /* Ctrl-O */
        connect(loadAct, &QAction::triggered, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, "Load Tuning", defaultTuningPath(),
                "JSON files (*.json);;All files (*)");
            if (!path.isEmpty())
                loadTuningFile(path);
        });

        fileMenu->addSeparator();
        QAction *quitAct = fileMenu->addAction("&Quit");
        quitAct->setShortcut(QKeySequence::Quit);   /* Ctrl-Q */
        connect(quitAct, &QAction::triggered, this, &QMainWindow::close);
    }

    void onTick()
    {
        /* Dongle mode: the chip is running vox_process; the host is
         * either pure viewer (no inject) or capturing PC audio and
         * shipping it down as INJECT_PCM (inject on).  In neither case
         * do we call vox_process locally. */
        if (sourceMode_ == SourceMode::Dongle) {
#ifdef VOX_QT_HAVE_DONGLE
            if (dongleInjectActive_ && audio_ && dongleClient_ &&
                dongleClient_->isOpen())
            {
                /* INJECT_PCM expects exactly VOX_INJECT_FRAME_SAMPLES
                 * (= kFrameSize at 8 kHz / 20 ms).  audio_io_read fills
                 * exactly frame_size samples per channel. */
                if (audio_io_read(audio_, micBuf_.data(), rxBuf_.data()) == 0) {
                    dongleClient_->sendInjectPcm(
                        micBuf_.data(), rxBuf_.data(), kFrameSize);
                }
            }
#endif
            if (++dongleStatusTickCount_ >= (1000 / kFrameMs)) {  /* ~1 Hz */
                dongleStatusTickCount_ = 0;
                statusBar()->showMessage(
                    QString("Dongle %1: %2 state frames received%3")
                        .arg(currentDonglePort_)
                        .arg(dongleStateFramesSeen_)
                        .arg(dongleInjectActive_ ? "  (inject ON)" : ""),
                    1500);
            }
            return;
        }

        if (!audio_ || !vox_)
            return;

        if (audio_io_read(audio_, micBuf_.data(), rxBuf_.data()) < 0)
            return;

        VoxConfig tuning = {};
        tuning.hang_ms = hangSlider_->value();
        tuning.mic_led_threshold = micThreshSlider_->value();
        tuning.rx_led_threshold = rxThreshSlider_->value();
        tuning.vad_led_prob_threshold = vadThreshSlider_->value();
        tuning.aec_led_reduction_pct = aecThreshSlider_->value();
        tuning.rx_guard_vad_boost = rxGuardVadBoostSlider_->value();
        tuning.rx_guard_snr_pct = rxGuardSnrSlider_->value();
        vox_set_tuning(vox_, &tuning);

        (void)vox_process(vox_, micBuf_.data(), rxBuf_.data());

        VoxLedState state = {};
        vox_get_led_state(vox_, &state);
        VoxDebugState debug = {};
        vox_get_debug_state(vox_, &debug);

        renderState(state, debug);
    }

    /* Drive all the on-screen widgets from a (LedState, DebugState) pair.
     * Source-agnostic: called both from onTick (local-mode pump) and
     * from the DongleClient::stateFrameReceived signal (chip-side pump).
     * No I/O here — pure widget updates so it's safe on the GUI thread. */
    void renderState(const VoxLedState &state, const VoxDebugState &debug)
    {
        QString micDbfsText = formatDbfs(state.mic_level);
        QString postDbfsText = formatDbfs(debug.mic_level_post_aec);
        QString rxDbfsText = formatDbfs(state.rx_level);

        micLevelBar_->setLevels(state.mic_level, debug.mic_level_post_aec,
                    micDbfsText.trimmed(), postDbfsText.trimmed());
        vadRawScoreBar_->setValue(state.vad_probability_raw);
        vadRawScoreBar_->setFormat(QString("%1%%").arg(state.vad_probability_raw));
        vadScoreBar_->setValue(state.vad_probability);
        vadScoreBar_->setFormat(QString("%1%%").arg(state.vad_probability));
        rxLevelBar_->setValue(state.rx_level);
        rxLevelBar_->setFormat(rxDbfsText);
        aecScoreBar_->setValue(state.aec_reduction_pct);
        aecScoreBar_->setFormat(QString("%1%% / %2 dB").arg(state.aec_reduction_pct).arg(ratioToDb(debug.mic_level_raw, debug.mic_level_post_aec), 0, 'f', 1));

        // LED on-colors picked for light bg + white text legibility.
        setLedLamp(micLedLamp_, state.mic_led != 0, "#16a34a");  // green
        setLedLamp(rxLedLamp_,  state.rx_led  != 0, "#2563eb");  // blue
        setLedLamp(vadLedLamp_, state.vad_led != 0, "#d97706");  // amber
        setLedLamp(aecLedLamp_, state.aec_led != 0, "#7c3aed");  // purple
        setLedLamp(pttLedLamp_, state.ptt_led != 0, "#dc2626");  // red

        if (state.ptt_led) {
            pttIndicator_->setText("ON");
            pttIndicator_->setStyleSheet(QString(
                "background:%1;color:%2;border-radius:6px;padding:6px;font-weight:700;")
                .arg(theme::kPttOnBg, theme::kPttOnText));
        } else {
            pttIndicator_->setText("OFF");
            pttIndicator_->setStyleSheet(QString(
                "background:%1;color:%2;border-radius:6px;padding:6px;font-weight:600;")
                .arg(theme::kPttOffBg, theme::kPttOffText));
        }

        double snrDb = ratioToDb(debug.mic_level_post_aec, debug.noise_floor);
        double aecAttenDb = ratioToDb(debug.mic_level_raw, debug.mic_level_post_aec);

        micPostAecValue_->setText(formatDbfs(debug.mic_level_post_aec));
        noiseFloorValue_->setText(formatDbfs(debug.noise_floor));
        snrValue_->setText(formatDb(snrDb));
        energyMarginValue_->setText(formatDb(aecAttenDb));
        rawVoiceValue_->setText(debug.voice_raw ? "yes" : "no");
        validatedVoiceValue_->setText(debug.voice_validated ? "yes" : "no");
        hangValueDisplay_->setText(QString("%1/%2")
            .arg(formatSignedInt(debug.hang_frames, 2))
            .arg(formatSignedInt(debug.hang_frames_max, 2)));
        rxActiveValue_->setText(debug.rx_active ? "yes" : "no");
        gateVadValue_->setText(QString("%1/%2")
            .arg(formatSignedInt(state.vad_probability, 3))
            .arg(formatSignedInt(debug.effective_vad_threshold, 3)));
        gateSnrValue_->setText(QString("%1/%2")
            .arg(formatSignedInt(debug.snr_pct, 3))
            .arg(formatSignedInt(debug.effective_snr_threshold, 3)));
        energyOkValue_->setText(debug.energy_ok ? "yes" : "no");
        if (debug.ptt_reason_voice)
            pttReasonValue_->setText("voice");
        else if (debug.ptt_reason_hang)
            pttReasonValue_->setText("hang");
        else
            pttReasonValue_->setText("off");
        residualCorrValue_->setText(QString("%1%%").arg(formatSignedInt(debug.residual_corr_pct, 3)));
        residualLevelValue_->setText(formatDbfsTenths(debug.residual_corr_dbfs_tenths));
        residualDelayPlot_->setProfile(debug);

        // Feed the time-series plot one frame's worth of state.
        HistoryFrame hf;
        hf.mic_raw_dbfs       = (float)levelToDbfs(debug.mic_level_raw);
        hf.mic_post_dbfs      = (float)levelToDbfs(debug.mic_level_post_aec);
        hf.rx_dbfs            = (float)levelToDbfs(debug.rx_level);
        hf.snr_db             = (float)snrDb;
        hf.vad_raw_pct        = state.vad_probability_raw;
        hf.vad_validated_pct  = state.vad_probability;
        hf.vad_threshold_pct  = debug.effective_vad_threshold;
        hf.ptt                = state.ptt_led != 0;
        timeSeriesPlot_->pushSample(hf);
        double peakMs = (double)debug.residual_corr_peak_delay_samples * 1000.0 / (double)kSampleRate;
        residualPeakLabel_->setText(
            QString("Peak: %1%% at %2 samples (%3 ms)")
                .arg(debug.residual_corr_peak_pct)
                .arg(debug.residual_corr_peak_delay_samples)
                .arg(peakMs, 0, 'f', 1));

        QStringList reasons;
        reasons << QString("mic=%1").arg(formatDbfs(debug.mic_level_raw))
            << QString("post=%1").arg(formatDbfs(debug.mic_level_post_aec))
            << QString("rx=%1").arg(formatDbfs(debug.rx_level))
            << QString("floor=%1").arg(formatDbfs(debug.noise_floor))
            << QString("snr=%1").arg(formatDb(snrDb))
            << QString("aec=%1").arg(formatDb(aecAttenDb))
                << QString("vadRaw=%1").arg(state.vad_probability_raw)
                << QString("vadFinal=%1").arg(state.vad_probability)
                << QString("voiceRaw=%1").arg(debug.voice_raw ? "yes" : "no")
                << QString("voice=%1").arg(debug.voice_validated ? "yes" : "no")
                << QString("guard=%1").arg(debug.rx_guard_applied ? "on" : "off")
                << QString("gate(v=%1/%2,s=%3/%4,e=%5)")
                    .arg(state.vad_probability)
                    .arg(debug.effective_vad_threshold)
                    .arg(debug.snr_pct)
                    .arg(debug.effective_snr_threshold)
                    .arg(debug.energy_ok ? "ok" : "low")
                << QString("res(c=%1%%,l=%2)")
                    .arg(debug.residual_corr_pct)
                    .arg(formatDbfsTenths(debug.residual_corr_dbfs_tenths).trimmed())
                << QString("ptt=%1").arg(state.ptt_led ? "on" : "off");
        decisionSummaryLabel_->setText(reasons.join("  |  "));
    }

    AudioIO *audio_ = nullptr;
    VoxState *vox_ = nullptr;

    std::array<int16_t, kFrameSize> micBuf_{};
    std::array<int16_t, kFrameSize> rxBuf_{};

    DualMicBar *micLevelBar_ = nullptr;
    QProgressBar *vadRawScoreBar_ = nullptr;
    QProgressBar *vadScoreBar_ = nullptr;
    QProgressBar *rxLevelBar_ = nullptr;
    QProgressBar *aecScoreBar_ = nullptr;
    QLabel *pttIndicator_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *decisionSummaryLabel_ = nullptr;
    QPlainTextEdit *dongleLogPanel_ = nullptr;

    QLabel *micPostAecValue_ = nullptr;
    QLabel *noiseFloorValue_ = nullptr;
    QLabel *snrValue_ = nullptr;
    QLabel *energyMarginValue_ = nullptr;
    QLabel *rawVoiceValue_ = nullptr;
    QLabel *validatedVoiceValue_ = nullptr;
    QLabel *hangValueDisplay_ = nullptr;
    QLabel *rxActiveValue_ = nullptr;
    QLabel *gateVadValue_ = nullptr;
    QLabel *gateSnrValue_ = nullptr;
    QLabel *energyOkValue_ = nullptr;
    QLabel *pttReasonValue_ = nullptr;
    QLabel *residualCorrValue_ = nullptr;
    QLabel *residualLevelValue_ = nullptr;
    QLabel *residualPeakLabel_ = nullptr;

    ResidualDelayPlot *residualDelayPlot_ = nullptr;
    TimeSeriesPlot    *timeSeriesPlot_    = nullptr;

    QLabel *micLedLamp_ = nullptr;
    QLabel *rxLedLamp_ = nullptr;
    QLabel *vadLedLamp_ = nullptr;
    QLabel *aecLedLamp_ = nullptr;
    QLabel *pttLedLamp_ = nullptr;

    QSlider *hangSlider_ = nullptr;
    QSlider *micThreshSlider_ = nullptr;
    QSlider *rxThreshSlider_ = nullptr;
    QSlider *vadThreshSlider_ = nullptr;
    QSlider *aecThreshSlider_ = nullptr;
    QSlider *rxGuardVadBoostSlider_ = nullptr;
    QSlider *rxGuardSnrSlider_ = nullptr;

    QLabel *hangValue_ = nullptr;
    QLabel *micThreshValue_ = nullptr;
    QLabel *rxThreshValue_ = nullptr;
    QLabel *vadThreshValue_ = nullptr;
    QLabel *aecThreshValue_ = nullptr;
    QLabel *rxGuardVadBoostValue_ = nullptr;
    QLabel *rxGuardSnrValue_ = nullptr;

    QComboBox *micDeviceCombo_ = nullptr;
    QComboBox *rxDeviceCombo_  = nullptr;

    /* ---- Source mode (Local vs Dongle) ---- */
    enum class SourceMode { Local, Dongle };
    SourceMode sourceMode_ = SourceMode::Local;
    QComboBox *sourceCombo_      = nullptr;
    QCheckBox *injectPcCheckbox_ = nullptr;
    bool       dongleInjectActive_ = false;
    QString    currentDonglePort_;   /* empty when sourceMode_ == Local */

#ifdef VOX_QT_HAVE_DONGLE
    DongleClient *dongleClient_ = nullptr;
    QString  dongleFwRevision_;
    quint32  dongleCapabilities_ = 0;
    quint32  dongleHwId_         = 0;
    /* Debounce timer for slider → SET_TUNING; lazy-built on first use. */
    QTimer *dongleTuningPushTimer_ = nullptr;
#endif
    /* Counters / timers used while in dongle mode to drive a status-bar
     * heartbeat without needing per-frame widget updates yet (Stage B). */
    int      dongleStateFramesSeen_ = 0;
    int      dongleStatusTickCount_ = 0;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // --quit-after-ms <ms> : fire QApplication::quit() after N ms (which
    // triggers closeEvent and the auto-save path).  Useful for headless
    // smoke testing the save/load round trip; not exposed in the GUI.
    int quit_after_ms = 0;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size() - 1; i++) {
        if (args[i] == "--quit-after-ms") {
            quit_after_ms = args[i + 1].toInt();
            break;
        }
    }

    // Anchor QStandardPaths::AppConfigLocation to ~/.config/vox/ on
    // Linux (and equivalents on macOS / Windows), independent of where
    // the binary lives or what it's renamed to.  We deliberately set
    // only the application name (no organization): with both set, the
    // path becomes ~/.config/<org>/<app>/, doubling the "vox" segment.
    QCoreApplication::setApplicationName("vox");

    // Force a light palette regardless of the system theme.  The "Fusion"
    // style respects palette settings consistently across desktops; the
    // platform-default style on KDE/GNOME may ignore some role colors.
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window,          QColor("#f8fafc"));
    palette.setColor(QPalette::WindowText,      QColor("#1e293b"));
    palette.setColor(QPalette::Base,            QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase,   QColor("#f1f5f9"));
    palette.setColor(QPalette::Text,            QColor("#1e293b"));
    palette.setColor(QPalette::Button,          QColor("#e2e8f0"));
    palette.setColor(QPalette::ButtonText,      QColor("#1e293b"));
    palette.setColor(QPalette::Highlight,       QColor("#3b82f6"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase,     QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipText,     QColor("#1e293b"));
    app.setPalette(palette);

    VoxWindow window;
    window.show();

    // Headless test hook: programmatically close the window after N ms.
    // window.close() goes through closeEvent so the auto-save runs;
    // app.quit() bypasses it.
    if (quit_after_ms > 0)
        QTimer::singleShot(quit_after_ms, &window, &QMainWindow::close);

    return app.exec();
}

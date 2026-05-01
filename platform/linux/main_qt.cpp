#include "audio_io.h"
#include "vox.h"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QProgressBar>
#include <QGridLayout>
#include <QLatin1Char>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

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
    label->setStyleSheet(
        "background:#20242a;"
        "color:#e8edf2;"
        "border:1px solid #4d5662;"
        "border-radius:5px;"
        "padding:4px 8px;"
        "font-family:monospace;");
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
    lamp->setStyleSheet(
        "background:#3b3b3b;"
        "color:#cfcfcf;"
        "border:1px solid #666;"
        "border-radius:10px;"
        "padding:4px;"
        "font-weight:600;");
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
            "border:1px solid #222;"
            "border-radius:10px;"
            "padding:4px;"
            "font-weight:700;").arg(onColor));
    } else {
        lamp->setStyleSheet(
            "background:#3b3b3b;"
            "color:#cfcfcf;"
            "border:1px solid #666;"
            "border-radius:10px;"
            "padding:4px;"
            "font-weight:600;");
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

        p.setPen(QPen(QColor("#4d5662"), 1));
        p.setBrush(QColor("#20242a"));
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
            p.fillRect(QRect(x0, y0, lowX - x0, h), QColor("#2f8f5a"));
        if (highX > lowX)
            p.fillRect(QRect(lowX, y0, highX - lowX, h), QColor("#a43a3a"));

        p.setPen(QPen(QColor("#f0f6ff"), 2));
        p.drawLine(rawX, y0, rawX, y0 + h - 1);
        p.setPen(QPen(QColor("#ffd66b"), 2));
        p.drawLine(postX, y0, postX, y0 + h - 1);

        const QString text = QString("M:%1  P:%2").arg(rawText_).arg(postText_);
        p.setPen(QColor("#f1f5f9"));
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
        p.setPen(QPen(QColor("#4d5662"), 1));
        p.setBrush(QColor("#131820"));
        p.drawRoundedRect(r, 6, 6);

        const QRect plot = r.adjusted(8, 8, -8, -22);
        if (plot.width() < 10 || plot.height() < 10)
            return;

        const int zeroIdx = (0 - minDelaySamples_) / std::max(1, stepSamples_);
        if (zeroIdx >= 0 && zeroIdx < VOX_RESIDUAL_CORR_PROFILE_POINTS) {
            const int zx = plot.left() + (zeroIdx * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            p.setPen(QPen(QColor("#3b475a"), 1, Qt::DashLine));
            p.drawLine(zx, plot.top(), zx, plot.bottom());
        }

        QPolygon poly;
        poly.reserve(VOX_RESIDUAL_CORR_PROFILE_POINTS);
        for (int i = 0; i < VOX_RESIDUAL_CORR_PROFILE_POINTS; i++) {
            int x = plot.left() + (i * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            int y = plot.bottom() - (profile_[i] * plot.height()) / 100;
            poly << QPoint(x, y);
        }

        p.setPen(QPen(QColor("#59c6ff"), 2));
        p.drawPolyline(poly);

        int peakIdx = (peakDelaySamples_ - minDelaySamples_) / std::max(1, stepSamples_);
        if (peakIdx >= 0 && peakIdx < VOX_RESIDUAL_CORR_PROFILE_POINTS) {
            int px = plot.left() + (peakIdx * plot.width()) / (VOX_RESIDUAL_CORR_PROFILE_POINTS - 1);
            int py = plot.bottom() - (profile_[peakIdx] * plot.height()) / 100;
            p.setPen(QPen(QColor("#ffd166"), 2));
            p.setBrush(QColor("#ffd166"));
            p.drawEllipse(QPoint(px, py), 3, 3);
        }

        p.setPen(QColor("#9aa6b2"));
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

class VoxWindow : public QMainWindow {
public:
    VoxWindow()
    {
        setWindowTitle("VOX Linux Test GUI");
        resize(860, 560);

        auto *root = new QWidget();
        auto *layout = new QVBoxLayout(root);

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
        pttIndicator_->setStyleSheet("background:#4a1d1d;color:#ffdede;border-radius:6px;padding:6px;font-weight:600;");
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
        residualPeakLabel_->setStyleSheet("font-family:monospace;color:#c9d6e2;");
        layout->addWidget(residualPeakLabel_);

        decisionSummaryLabel_ = new QLabel("Waiting for first audio frame...");
        decisionSummaryLabel_->setWordWrap(true);
        decisionSummaryLabel_->setStyleSheet(
            "background:#182028;"
            "color:#d9e7f2;"
            "border:1px solid #425063;"
            "border-radius:6px;"
            "padding:8px;"
            "font-family:monospace;");
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

        statusLabel_ = new QLabel("Initializing audio...");
        layout->addWidget(statusLabel_);

        setCentralWidget(root);

        AudioIOConfig acfg = {};
        acfg.mic_device = "auto";
        acfg.rx_device = "auto";
        acfg.sample_rate = kSampleRate;
        acfg.frame_size = kFrameSize;

        audio_ = audio_io_open(&acfg);
        if (!audio_) {
            statusLabel_->setText("Audio open failed. Check PulseAudio/PipeWire sources.");
            return;
        }

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
            audio_io_close(audio_);
            audio_ = nullptr;
            return;
        }

        statusLabel_->setText("Running: auto-selected mic + default speaker monitor.");

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

private:
    void onTick()
    {
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

        setLedLamp(micLedLamp_, state.mic_led != 0, "#2d7d46");
        setLedLamp(rxLedLamp_, state.rx_led != 0, "#20688a");
        setLedLamp(vadLedLamp_, state.vad_led != 0, "#8a5a22");
        setLedLamp(aecLedLamp_, state.aec_led != 0, "#6f3ea0");
        setLedLamp(pttLedLamp_, state.ptt_led != 0, "#a32d2d");

        if (state.ptt_led) {
            pttIndicator_->setText("ON");
            pttIndicator_->setStyleSheet("background:#1f6f3b;color:#e6ffe6;border-radius:6px;padding:6px;font-weight:700;");
        } else {
            pttIndicator_->setText("OFF");
            pttIndicator_->setStyleSheet("background:#4a1d1d;color:#ffdede;border-radius:6px;padding:6px;font-weight:600;");
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
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    VoxWindow window;
    window.show();

    return app.exec();
}

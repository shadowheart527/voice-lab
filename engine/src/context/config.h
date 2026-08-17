#ifndef MAIN_CONTEXT_CONFIG_H
#define MAIN_CONTEXT_CONFIG_H

#include <QObject>
#include "../filesystem.hpp"
#include <fstream>
#include <toml++/toml.h>

#include "solvermakers.h"
#include "../gui/qpainterwrapper.h"
#include "../modules/audio/base/base.h"

namespace Main {

    fs::path getConfigPath();
    toml::table getConfigTable();

    class Config : public QObject {
        Q_OBJECT
        Q_PROPERTY(int pitchAlgorithm       READ getPitchAlgorithmNumeric       WRITE setPitchAlgorithm         NOTIFY pitchAlgorithmChanged)
        Q_PROPERTY(int linpredAlgorithm     READ getLinpredAlgorithmNumeric     WRITE setLinpredAlgorithm       NOTIFY linpredAlgorithmChanged)
        Q_PROPERTY(int formantAlgorithm     READ getFormantAlgorithmNumeric     WRITE setFormantAlgorithm       NOTIFY formantAlgorithmChanged)
        Q_PROPERTY(int invglotAlgorithm     READ getInvglotAlgorithmNumeric     WRITE setInvglotAlgorithm       NOTIFY invglotAlgorithmChanged)
        Q_PROPERTY(double viewZoom          READ getViewZoom                    WRITE setViewZoom               NOTIFY viewZoomChanged)
        Q_PROPERTY(int viewMinFrequency     READ getViewMinFrequency            WRITE setViewMinFrequency       NOTIFY viewMinFrequencyChanged)
        Q_PROPERTY(int viewMaxFrequency     READ getViewMaxFrequency            WRITE setViewMaxFrequency       NOTIFY viewMaxFrequencyChanged)
        Q_PROPERTY(int viewFFTSize          READ getViewFFTSize                 WRITE setViewFFTSize            NOTIFY viewFFTSizeChanged)
        Q_PROPERTY(int viewMaxGain          READ getViewMaxGain                 WRITE setViewMaxGain            NOTIFY viewMaxGainChanged)
        Q_PROPERTY(double viewTimeSpan      READ getViewTimeSpan                WRITE setViewTimeSpan           NOTIFY viewTimeSpanChanged)
        Q_PROPERTY(int viewFrequencyScale   READ getViewFrequencyScaleNumeric   WRITE setViewFrequencyScale     NOTIFY viewFrequencyScaleChanged)
        Q_PROPERTY(bool viewShowSpectrogram READ getViewShowSpectrogram         WRITE setViewShowSpectrogram    NOTIFY viewShowSpectrogramChanged)
        Q_PROPERTY(bool viewShowPitch       READ getViewShowPitch               WRITE setViewShowPitch          NOTIFY viewShowPitchChanged)
        Q_PROPERTY(bool viewShowFormants    READ getViewShowFormants            WRITE setViewShowFormants       NOTIFY viewShowFormantsChanged)
        Q_PROPERTY(bool paused              READ isPaused                       WRITE setPaused                 NOTIFY pausedChanged)
        Q_PROPERTY(double targetPitchMin    READ getTargetPitchMin              WRITE setTargetPitchMin         NOTIFY targetPitchMinChanged)
        Q_PROPERTY(double targetPitchMax    READ getTargetPitchMax              WRITE setTargetPitchMax         NOTIFY targetPitchMaxChanged)
        Q_PROPERTY(bool viewShowTargetBand  READ getViewShowTargetBand          WRITE setViewShowTargetBand     NOTIFY viewShowTargetBandChanged)
        Q_PROPERTY(bool viewShowHud         READ getViewShowHud                 WRITE setViewShowHud            NOTIFY viewShowHudChanged)
        Q_PROPERTY(bool uiShowSidebar       READ getUiShowSidebar               WRITE setUiShowSidebar          NOTIFY uiShowSidebarChanged)
        Q_PROPERTY(bool analysisDenoise     READ getAnalysisDenoise             WRITE setAnalysisDenoise        NOTIFY analysisDenoiseChanged)
        Q_PROPERTY(bool viewShowFormantBands READ getViewShowFormantBands       WRITE setViewShowFormantBands   NOTIFY viewShowFormantBandsChanged)
        Q_PROPERTY(bool uiLightMode         READ getUiLightMode                 WRITE setUiLightMode            NOTIFY uiLightModeChanged)
        Q_PROPERTY(bool viewTrackLines      READ getViewTrackLines              WRITE setViewTrackLines         NOTIFY viewTrackLinesChanged)
        Q_PROPERTY(bool viewGenderColors    READ getViewGenderColors            WRITE setViewGenderColors       NOTIFY viewGenderColorsChanged)
        Q_PROPERTY(bool viewShowHistory     READ getViewShowHistory             WRITE setViewShowHistory        NOTIFY viewShowHistoryChanged)
        Q_PROPERTY(double viewHistorySpan   READ getViewHistorySpan             WRITE setViewHistorySpan        NOTIFY viewHistorySpanChanged)
        Q_PROPERTY(bool viewHistoryArea     READ getViewHistoryArea             WRITE setViewHistoryArea        NOTIFY viewHistoryAreaChanged)

    signals:
        void pitchAlgorithmChanged(int);
        void linpredAlgorithmChanged(int);
        void formantAlgorithmChanged(int);
        void invglotAlgorithmChanged(int);
        void audioBackendChanged(int);
        void viewZoomChanged(double);
        void viewMinFrequencyChanged(int);
        void viewMaxFrequencyChanged(int);
        void viewFFTSizeChanged(int);
        void viewMaxGainChanged(int);
        void viewTimeSpanChanged(double);
        void viewFrequencyScaleChanged(int);
        void viewShowSpectrogramChanged(bool);
        void viewShowPitchChanged(bool);
        void viewShowFormantsChanged(bool);
        void pausedChanged(bool);
        void targetPitchMinChanged(double);
        void targetPitchMaxChanged(double);
        void viewShowTargetBandChanged(bool);
        void viewShowHudChanged(bool);
        void uiShowSidebarChanged(bool);
        void analysisDenoiseChanged(bool);
        void viewShowFormantBandsChanged(bool);
        void uiLightModeChanged(bool);
        void viewTrackLinesChanged(bool);
        void viewGenderColorsChanged(bool);
        void viewShowHistoryChanged(bool);
        void viewHistorySpanChanged(double);
        void viewHistoryAreaChanged(bool);

    public:
        Config();
        virtual ~Config();

        void save();
        
        Module::Audio::Backend getAudioBackend();
        void setAudioBackend(Module::Audio::Backend b);

        PitchAlgorithm getPitchAlgorithm();
        void setPitchAlgorithm(PitchAlgorithm alg);
        
        int getPitchAlgorithmNumeric();
        void setPitchAlgorithm(int alg);

        LinpredAlgorithm getLinpredAlgorithm();
        void setLinpredAlgorithm(LinpredAlgorithm alg);
        
        int getLinpredAlgorithmNumeric();
        void setLinpredAlgorithm(int alg);

        FormantAlgorithm getFormantAlgorithm(bool internal = false);
        void setFormantAlgorithm(FormantAlgorithm alg);
        
        int getFormantAlgorithmNumeric();
        void setFormantAlgorithm(int alg);

        InvglotAlgorithm getInvglotAlgorithm();
        void setInvglotAlgorithm(InvglotAlgorithm alg);
        
        int getInvglotAlgorithmNumeric();
        void setInvglotAlgorithm(int alg);

        double getViewZoom();
        void setViewZoom(double scale);

        int getViewMinFrequency();
        void setViewMinFrequency(int f);

        int getViewMaxFrequency();
        void setViewMaxFrequency(int f);

        int getViewFFTSize();
        void setViewFFTSize(int nfft);

        int getViewMaxGain();
        void setViewMaxGain(int g);

        double getViewTimeSpan();
        void setViewTimeSpan(double dur);

        FrequencyScale getViewFrequencyScale();
        void setViewFrequencyScale(FrequencyScale scale);

        int getViewFrequencyScaleNumeric();
        void setViewFrequencyScale(int scale);

        int getViewFormantCount();
        std::tuple<double, double, double> getViewFormantColor(int i);

        bool getViewShowSpectrogram();
        void setViewShowSpectrogram(bool b);

        bool getViewShowPitch();
        void setViewShowPitch(bool b);

        bool getViewShowFormants();
        void setViewShowFormants(bool b);

        bool getViewFormantSmoothing();
        void setViewFormantSmoothing(bool b);

        bool getViewPitchSmoothing();
        void setViewPitchSmoothing(bool b);

        double getTargetPitchMin();
        void setTargetPitchMin(double hz);

        double getTargetPitchMax();
        void setTargetPitchMax(double hz);

        double getMascPitchMin();
        double getMascPitchMax();

        double getFormantBand(int formant, bool fem, bool upper);

        bool getViewShowFormantBands();
        void setViewShowFormantBands(bool b);

        bool getViewShowTargetBand();
        void setViewShowTargetBand(bool b);

        bool getViewShowHud();
        void setViewShowHud(bool b);

        bool getUiShowSidebar();
        void setUiShowSidebar(bool b);

        bool getUiLightMode();
        void setUiLightMode(bool b);

        bool getViewTrackLines();
        void setViewTrackLines(bool b);

        bool getViewGenderColors();
        void setViewGenderColors(bool b);

        bool getViewShowHistory();
        void setViewShowHistory(bool b);

        double getViewHistorySpan();
        void setViewHistorySpan(double s);

        bool getViewHistoryArea();
        void setViewHistoryArea(bool b);

        bool getAnalysisDenoise();
        void setAnalysisDenoise(bool b);

        bool getFeedEnabled();
        int getFeedPort();

        int getAnalysisMaxFrequency();
        int getAnalysisLpOffset();
        int getAnalysisPitchSampleRate();

        void setAnalysisGranularity(double ms); // default is 20ms
        double getAnalysisGranularity();

        void setAnalysisSpectrogramWindow(double ms); // default is 50ms
        double getAnalysisSpectrogramWindow();

        void setAnalysisPitchWindow(double ms); // default is 40ms
        double getAnalysisPitchWindow();

        void setAnalysisFormantWindow(double ms); // default is 20ms
        double getAnalysisFormantWindow();

        void setAnalysisOscilloscopeWindow(double ms); // default is 80ms
        double getAnalysisOscilloscopeWindow();

        void setAnalysisPitchSpacing(double ms); // default is 80ms
        double getAnalysisPitchSpacing();

        void setAnalysisFormantSpacing(double ms); // default is 80ms
        double getAnalysisFormantSpacing();

        void setAnalysisOscilloscopeSpacing(double ms); // default is 160ms
        double getAnalysisOscilloscopeSpacing();

        // WILL NOT BE SERIALIZED
        bool isPaused();
        void setPaused(bool p);

    private:
        toml::table mTbl;
    
        // WILL NOT BE SERIALIZED
        bool mPaused;
    };

}

#endif // MAIN_CONTEXT_CONFIG_H

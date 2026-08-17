import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import IfCanvas 

ApplicationWindow {
    id: mainWindow
    title: appName
    visible: false

    width: 640
    height: 480

    Material.theme: config.uiLightMode ? Material.Light : Material.Dark
    Material.accent: Material.DeepPurple

    function mel(f) {
        return 2595 * Math.log10(1 + f / 700);
    }

    function hz(m) {
        return 700 * (Math.pow(10, m / 2595) - 1);
    }

    // Every option gets a plain-language hover tooltip. The width cap makes long
    // explanations wrap (the Material tooltip text already has wrapMode set, it
    // just never wraps unless the popup is narrower than its implicit size).
    component HelpTip: ToolTip {
        visible: parent ? (parent.hovered === true && !(parent.popup && parent.popup.visible)) : false
        delay: 500
        width: Math.min(implicitWidth, 320)
    }

    // A caption label that shows the same tooltip as the control below it.
    component TipLabel: Label {
        property alias tip: tipPopup.text
        HoverHandler { id: labelHover }
        HelpTip { id: tipPopup; visible: labelHover.hovered }
    }

    // Shared texts for label + control pairs, so hovering either shows the same thing.
    readonly property string tipHistoryLength: "How far back the history strips reach, from 30 seconds to 10 minutes. Short shows moment-to-moment movement; long shows the shape of a whole session. Lengthening it mid-session brings earlier history back into view."
    readonly property string tipPitchTarget: "The pitch range you're aiming for. It draws the pink band and drives the time-in-target session stat. 165 to 220 Hz is a common fem goal, but set it to whatever you're actually working toward."
    readonly property string tipViewDuration: "How many seconds of voice fit across the screen. Shorter scrolls faster and magnifies detail; longer shows more context at once."
    readonly property string tipFreqRange: "The lowest and highest frequencies the graph shows. Everything that matters for voice sits below about 4000 Hz, so narrowing the range just gives the useful part more room."
    readonly property string tipFreqScale: "How frequency is spread up the screen. Linear gives every Hz equal space, which squashes the voice range into the bottom of the graph. The other three give low frequencies more room; Mel and ERB are spaced the way human hearing is. Pick whichever looks clearest to you."
    readonly property string tipFftSize: "Sharpness trade-off for the spectrogram picture only; the tracked lines are unaffected. Bigger is crisper in frequency but blurrier in time, smaller is the reverse. The default is a sensible middle."
    readonly property string tipGain: "How quiet a sound can be and still appear in the spectrogram. If the background looks speckled and busy, lower this. If softer parts of your voice vanish, raise it."
    readonly property string tipPitchAlg: "Three ways of measuring the same thing. On a clean mic signal they agree, and they differ mostly in how they fail. If the pitch line drops out or jumps around for you, try another one; otherwise leave it."
    readonly property string tipFormantAlg: "How the formants are found. Simple LPC is the fast standard. Filtered LPC cleans the signal first and tends to be steadier on higher pitched voices, which matters when you're working in fem range. DeepFormants uses a neural network: sometimes more accurate, noticeably heavier on the computer."
    readonly property string tipLpcAlg: "Internal math variant used by the two LPC formant methods above. The differences are subtle; Burg is usually considered the most reliable for speech. Fine to leave alone forever."
    readonly property string tipInvglot: "Estimates what your vocal folds are doing before your mouth shapes the sound. Nothing on this screen uses it; it only feeds the oscilloscope window's glottal view. Safe to ignore."

    Shortcut {
        sequence: "Space"
        onActivated: config.paused = !config.paused
    }

    header: ToolBar {
        Material.background: config.uiLightMode
                ? Material.color(Material.BlueGrey, Material.Shade100)
                : Material.color(Material.BlueGrey, Material.Shade800)

        RowLayout {
            anchors.fill: parent

            RowLayout {
                Layout.alignment: Qt.AlignLeft

                spacing: 0

                ToolButton {
                    id: drawerButton
                    icon.source: drawer.visible ? "qrc:/icons/menu_open.svg" : "qrc:/icons/menu.svg"
                    checked: drawer.visible
                    HelpTip { text: "Show or hide the settings sidebar." }
                    onPressed: {
                        drawer.visible = !drawer.visible
                        config.uiShowSidebar = drawer.visible
                    }

                    transform: [
                        Rotation {
                            angle: 90 * drawer.position
                            origin.x: drawerButton.x + drawerButton.width / 2
                            origin.y: drawerButton.y + drawerButton.height / 2
                        },
                        Scale {
                            xScale: 1 - 0.15 * drawer.position
                            yScale: 1 - 0.15 * drawer.position
                            origin.x: drawerButton.x + drawerButton.width / 2
                            origin.y: drawerButton.y + drawerButton.height / 2
                        }
                    ]

                    Component.onCompleted: {
                        icon.width = 2 * icon.width
                        icon.height = 2 * icon.height
                    }
                }

                RowLayout {
                    Layout.fillHeight: true

                    id: zoomPanel

                    spacing: -2

                    ToolButton {
                        Layout.alignment: Qt.AlignLeft
                        icon.source: "qrc:/icons/zoom_out.svg"
                        onPressed: if (config.viewZoom <= 2.0) config.viewZoom -= 0.1; else config.viewZoom -= 0.5
                        enabled: config.viewZoom >= 0.6
                        HelpTip { text: "Make everything drawn on the graph smaller, like display scaling. It doesn't change how much time or frequency you see." }
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: (Math.round(100 * config.viewZoom)) + "%"
                        color: config.uiLightMode ? "#454055" : "lightgrey"
                    }

                    ToolButton {
                        Layout.alignment: Qt.AlignRight
                        icon.source: "qrc:/icons/zoom_in.svg"
                        onPressed: if (config.viewZoom < 2.0) config.viewZoom += 0.1; else config.viewZoom += 0.5
                        enabled: config.viewZoom < 5.0
                        HelpTip { text: "Make everything drawn on the graph bigger, like display scaling. It doesn't change how much time or frequency you see." }
                    }
                }

                ToolButton {
                    icon.source: config.paused ? "qrc:/icons/play_arrow.svg" : "qrc:/icons/stop.svg"
                    checked: config.paused
                    onPressed: config.paused = !config.paused
                    HelpTip { text: "Freeze the display so you can study what you just said, then resume. The space bar does the same." }

                    Component.onCompleted: {
                        icon.width = 2 * icon.width
                        icon.height = 2 * icon.height
                    }
                }
            }
            
            RowLayout {
                Layout.alignment: Qt.AlignRight

                spacing: 1

                Button {
                    text: "Oscilloscope"
                    Material.background: Material.color(Material.DeepPurple, Material.ShadeA200)
                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: 10
                    onPressed: oscilloscopeWindow.show()
                    HelpTip { text: "Open a second window showing the raw waveform of your voice, plus an estimate of the vocal fold motion behind it. Not needed for everyday training." }
                } 

                Button {
                    visible: HAS_SYNTH
                    text: "Synthesizer"
                    Material.background: Material.color(Material.DeepPurple, Material.ShadeA200)
                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: 10
                    onPressed: synthWindow.show()
                    HelpTip { text: "Open the built-in speech synthesizer, an upstream extra for generating test sounds. Not part of voice training." }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: config.uiLightMode ? "#faf9fc" : "black"
    }

    Drawer {
        id: drawer
        position: 0.0
        visible: position > 0
        y: header.height
        width: sidebar.width
        height: parent.height - header.height
        modal: false

        Flickable {
            id: sidebar
            width: sidebarContent.width
            height: parent.height
            contentWidth: sidebarContent.width
            contentHeight: sidebarContent.height
            clip: true

            ScrollBar.vertical: ScrollBar {}

            Column {
                id: sidebarContent
                
                padding: 20

                ColumnLayout {

                    Behavior on x {
                        NumberAnimation {
                            easing.type: Easing.InOutQuad
                            duration: 200
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation {
                            easing.type: Easing.InOutQuad
                            duration: 200
                        }
                    }

                    Switch {
                        text: "Light mode"
                        checked: config.uiLightMode
                        onToggled: config.uiLightMode = checked
                        HelpTip { text: "Light or dark look for the whole app. Purely cosmetic, pick whichever is easier on your eyes." }
                    }

                    MenuSeparator {}

                    Switch {
                        text: "Spectrogram"
                        checked: config.viewShowSpectrogram
                        onToggled: config.viewShowSpectrogram = checked
                        HelpTip { text: "The colored background picture: every frequency in your voice at once, brighter where louder. Nice context, but the lines carry the actual training information, so turning this off gives a cleaner view." }
                    }

                    Switch {
                        text: "Pitch track"
                        checked: config.viewShowPitch
                        onToggled: config.viewShowPitch = checked
                        HelpTip { text: "The line tracing the note your voice sits on. Pitch is the single biggest factor in how a voice is gendered, so this is the main line to watch." }
                    }

                    Switch {
                        text: "Formant tracks"
                        checked: config.viewShowFormants
                        onToggled: config.viewShowFormants = checked
                        HelpTip { text: "The F1, F2 and F3 lines: resonance, the way your throat and mouth color the sound. Resonance is why two voices at the same pitch can read differently, and it's the harder half of training." }
                    }

                    Switch {
                        text: "Track lines"
                        checked: config.viewTrackLines
                        onToggled: config.viewTrackLines = checked
                        HelpTip { text: "Draw the tracks as connected lines instead of strings of dots. Lines are easier to follow by eye; dots show each raw measurement separately. Same data either way." }
                    }

                    Switch {
                        text: "Gender colors"
                        checked: config.viewGenderColors
                        onToggled: config.viewGenderColors = checked
                        HelpTip { text: "Tint each track by how that measurement reads, blue (masc) through grey to pink (fem), so a glance shows where you are without reading numbers. Off keeps each track a single plain color." }
                    }

                    MenuSeparator {}

                    Label { text: "Voice targets:" }

                    Switch {
                        text: "Range bands"
                        checked: config.viewShowTargetBand
                        onToggled: config.viewShowTargetBand = checked
                        HelpTip { text: "Shaded pitch zones on the graph: blue marks a typical masc speaking range, pink marks the fem target range you set below. The goal is keeping the pitch line inside the pink." }
                    }

                    Switch {
                        text: "Formant bands"
                        checked: config.viewShowFormantBands
                        onToggled: config.viewShowFormantBands = checked
                        HelpTip { text: "The same idea for resonance: masc and fem zones drawn around F1, F2 and F3, so you can see whether your resonance is sitting fem even when your pitch already is." }
                    }

                    Switch {
                        text: "Live readout"
                        checked: config.viewShowHud
                        onToggled: config.viewShowHud = checked
                        HelpTip { text: "The stats box: current pitch and note, formant values, the male/andro/female read meter, and session stats (median pitch, time in target, duration). Press R to restart the session stats." }
                    }

                    Switch {
                        text: "Noise gate"
                        checked: config.analysisDenoise
                        onToggled: config.analysisDenoise = checked
                        HelpTip { text: "Only track sound that appears to be a voice, so fans, typing and room noise don't smear garbage across the graphs. If it ever clips the start or end of your words, turn it off." }
                    }

                    Switch {
                        text: "History graphs"
                        checked: config.viewShowHistory
                        onToggled: config.viewShowHistory = checked
                        HelpTip { text: "The strips in the top right corner: how your pitch, resonance and overall read have trended over the past minutes. Good for spotting drift, like your voice sliding down as you tire, and for seeing a session's progress." }
                    }

                    Switch {
                        text: "History fill"
                        checked: config.viewHistoryArea
                        onToggled: config.viewHistoryArea = checked
                        HelpTip { text: "Fill the history strips solid, pink below the curve and blue above, instead of drawing a thin line. Same information, just readable from further away." }
                    }

                    TipLabel {
                        text: "History length: " + (config.viewHistorySpan >= 60
                                ? (Math.round(config.viewHistorySpan / 60 * 10) / 10) + " min"
                                : Math.round(config.viewHistorySpan) + " s")
                        tip: tipHistoryLength
                    }
                    Slider {
                        from: 30
                        to: 600
                        stepSize: 30
                        value: config.viewHistorySpan
                        onMoved: config.viewHistorySpan = value
                        HelpTip { text: tipHistoryLength }
                    }

                    TipLabel {
                        text: "Pitch target range: "
                              + Math.round(config.targetPitchMin) + " - "
                              + Math.round(config.targetPitchMax) + " Hz"
                        tip: tipPitchTarget
                    }
                    RangeSlider {
                        from: 80
                        to: 400
                        first.value: config.targetPitchMin
                        first.onMoved: config.targetPitchMin = Math.round(first.value)
                        second.value: config.targetPitchMax
                        second.onMoved: config.targetPitchMax = Math.round(second.value)
                        HelpTip { text: tipPitchTarget }
                    }

                    MenuSeparator {}

                    TipLabel { text: "View duration:"; tip: tipViewDuration }
                    Slider {
                        from: 2
                        to: 30
                        stepSize: 0.5
                        value: config.viewTimeSpan
                        onMoved: config.viewTimeSpan = value
                        HelpTip { text: tipViewDuration }
                        Label {
                            id: timeSpanHandleLabel
                            anchors.top: parent.handle.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.handle.horizontalCenter
                            text: config.viewTimeSpan + " s"
                        }
                        Layout.bottomMargin: timeSpanHandleLabel.height - 10
                    }

                    MenuSeparator {}

                    TipLabel { text: "View frequency range:"; tip: tipFreqRange }
                    RangeSlider {
                        from: mel(1)
                        to: mel(16000)
                        first.value: mel(config.viewMinFrequency)
                        first.onMoved: config.viewMinFrequency = Math.max(1, hz(first.value))
                        second.value: mel(config.viewMaxFrequency)
                        second.onMoved: config.viewMaxFrequency = Math.max(100, hz(second.value))
                        HelpTip { text: tipFreqRange }
                        Label {
                            anchors.top: parent.first.handle.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.first.handle.horizontalCenter
                            text: config.viewMinFrequency + " Hz"
                        }
                        Label {
                            id: frequencyRangeHandleLabel
                            anchors.top: parent.second.handle.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.second.handle.horizontalCenter
                            text: config.viewMaxFrequency + " Hz"
                        }
                        Layout.bottomMargin: frequencyRangeHandleLabel.height - 10
                    }

                    MenuSeparator {}

                    TipLabel { text: "View frequency scale:"; tip: tipFreqScale }
                    ComboBox {
                        implicitWidth: parent.width - 10
                        model: [ "Linear", "Logarithmic", "Mel", "ERB" ]
                        currentIndex: config.viewFrequencyScale
                        onActivated: config.viewFrequencyScale = currentIndex
                        HelpTip { text: tipFreqScale }
                        Layout.alignment: Qt.AlignHCenter
                    }

                    MenuSeparator {}
                    
                    TipLabel { text: "FFT size"; tip: tipFftSize }
                    Slider {
                        from: Math.log2(512)
                        to: Math.log2(4096)
                        stepSize: 1
                        value: Math.log2(config.viewFFTSize)
                        onMoved: config.viewFFTSize = Math.pow(2, Math.round(value))
                        HelpTip { text: tipFftSize }
                        Label {
                            id: fftSizeHandleLabel
                            anchors.top: parent.handle.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.handle.horizontalCenter
                            text: config.viewFFTSize
                        }
                        Layout.bottomMargin: fftSizeHandleLabel.height - 10
                    }

                    MenuSeparator {}

                    TipLabel { text: "Gain normalization:"; tip: tipGain }
                    Slider {
                        from: -100
                        to: 40
                        value: config.viewMaxGain
                        onMoved: config.viewMaxGain = value
                        HelpTip { text: tipGain }
                        Label {
                            id: gainHandleLabel
                            anchors.top: parent.handle.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.handle.horizontalCenter
                            text: config.viewMaxGain + " dB"
                        }
                        Layout.bottomMargin: gainHandleLabel.height - 10
                    }

                    MenuSeparator {}

                    TipLabel { text: "Pitch algorithm:"; tip: tipPitchAlg }
                    ComboBox {
                        implicitWidth: parent.width - 10
                        model: [ "YIN", "McLeod", "RAPT" ]
                        currentIndex: config.pitchAlgorithm
                        onActivated: config.pitchAlgorithm = currentIndex
                        HelpTip { text: tipPitchAlg }
                        Layout.alignment: Qt.AlignHCenter
                    }
         
                    MenuSeparator {}

                    TipLabel { text: "Formant algorithm:"; tip: tipFormantAlg }
                    ComboBox {
                        implicitWidth: parent.width - 10
                        model: HAS_TORCH
                                    ? [ "Simple LPC", "Filtered LPC", "DeepFormants" ]
                                    : [ "Simple LPC", "Filtered LPC" ]
                        currentIndex: config.formantAlgorithm
                        onActivated: config.formantAlgorithm = currentIndex
                        HelpTip { text: tipFormantAlg }
                        Layout.alignment: Qt.AlignHCenter
                    }

                    MenuSeparator {}

                    TipLabel { text: "LPC algorithm:"; tip: tipLpcAlg }
                    ComboBox {
                        implicitWidth: parent.width - 10
                        model: [ "Autocorrelation", "Covariance", "Burg" ]
                        currentIndex: config.linpredAlgorithm
                        onActivated: config.linpredAlgorithm = currentIndex
                        HelpTip { text: tipLpcAlg }
                        Layout.alignment: Qt.AlignHCenter
                    }

                    MenuSeparator {}

                    TipLabel { text: "Glottal inverse algorithm:"; tip: tipInvglot }
                    ComboBox {
                        implicitWidth: parent.width - 10
                        //model: [ "IAIF", "GFM-IAIF", "AM-GIF" ]
                        model: [ "IAIF", "GFM-IAIF" ]
                        currentIndex: config.invglotAlgorithm
                        onActivated: config.invglotAlgorithm = currentIndex
                        HelpTip { text: tipInvglot }
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }

    IfCanvas {
        id: canvas
        // NOTE: anchoring to header.bottom is invalid here -- the ToolBar assigned
        // to ApplicationWindow.header lives in the window's header container, not in
        // contentItem (which is this item's parent). Qt silently drops cross-parent
        // anchors, which left the canvas at height 0 and the view permanently black.
        // contentItem already starts below the header, so anchor to parent.top.
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right

        Behavior on x {
            NumberAnimation {
                easing.type: Easing.InOutQuad
                duration: 200
            }
        }

        Behavior on width {
            NumberAnimation {
                easing.type: Easing.InOutQuad
                duration: 200
            }
        }

        // If in portrait mode, don't push the canvas when drawer is open.
        width: ((mainWindow.height > mainWindow.width) 
                    ? parent.width  
                    : parent.width - drawer.position * sidebar.width)
    }

    Timer {
        repeat: false; running: true; interval: 10
        onTriggered: {
            // (This used to assign undefined Flickable states, which did nothing:
            // the saved sidebar preference was never actually restored.)
            if (config.uiShowSidebar)
                drawer.visible = true
        }
    }

    property SynthWindow synthWindow

    OscilloscopeWindow {
        id: oscilloscopeWindow
    }

    Component.onCompleted: {
        if (HAS_SYNTH) {
            synthWindow = Qt.createQmlObject('SynthWindow {}', mainWindow);
        }
    }
}


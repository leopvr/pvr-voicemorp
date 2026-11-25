// PVR VoiceMorph - interfaz (blanco y negro).
#include "PluginEditor.h"

namespace Colours_
{
    static const juce::Colour bg     { 0xff0a0a0a };
    static const juce::Colour panel  { 0xff151515 };
    static const juce::Colour line   { 0xff2a2a2a };
    static const juce::Colour dim    { 0xff7a7a7a };
    static const juce::Colour fg     { 0xfff2f2f2 };
}

static const char* slotLetters[] = { "A", "B", "C", "D" };

//==============================================================================
PVRLookAndFeel::PVRLookAndFeel()
{
    using namespace Colours_;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Slider::textBoxTextColourId, fg);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, fg);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, fg);
    setColour (juce::TextButton::textColourOffId, fg);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, fg);
    setColour (juce::ComboBox::arrowColourId, fg);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, fg);
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, fg);
    setColour (juce::TextEditor::backgroundColourId, bg);
    setColour (juce::TextEditor::textColourId, fg);
    setColour (juce::TextEditor::outlineColourId, line);
}

void PVRLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    using namespace Colours_;
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // arco de valor: desde el centro para parámetros bipolares
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path val;
    val.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (fg);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (panel);
    g.fillEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
    const juce::Point<float> tip (c.x + std::sin (angle) * r * 0.6f, c.y - std::cos (angle) * r * 0.6f);
    g.setColour (fg);
    g.drawLine ({ c, tip }, 2.0f);
}

//==============================================================================
XYPad::XYPad (PVRVoiceMorphProcessor& p)
    : proc (p),
      xAttach (*p.apvts.getParameter ("padX"), [this] (float v) { x = v; repaint(); }),
      yAttach (*p.apvts.getParameter ("padY"), [this] (float v) { y = v; repaint(); })
{
    xAttach.sendInitialUpdate();
    yAttach.sendInitialUpdate();
    refreshLabels();
}

void XYPad::refreshLabels()
{
    labels.clear();
    for (int s = 0; s < PVRVoiceMorphProcessor::numSlots; ++s) labels.add (proc.getSlotLabel (s));
    repaint();
}

juce::Rectangle<float> XYPad::padArea() const { return getLocalBounds().toFloat().reduced (1.0f); }

void XYPad::paint (juce::Graphics& g)
{
    using namespace Colours_;
    const auto a = padArea();
    g.setColour (panel);
    g.fillRect (a);
    g.setColour (line);
    for (int i = 1; i < 8; ++i)
    {
        const float fx = a.getX() + a.getWidth() * (float) i / 8.0f;
        const float fy = a.getY() + a.getHeight() * (float) i / 8.0f;
        g.drawVerticalLine ((int) fx, a.getY(), a.getBottom());
        g.drawHorizontalLine ((int) fy, a.getX(), a.getRight());
    }
    g.drawRect (a, 1.0f);

    // etiquetas de las esquinas
    g.setFont (juce::FontOptions (13.0f));
    const auto corner = [&] (int s, juce::Justification j)
    {
        const bool empty = ! proc.getSlot (s).isValid();
        g.setColour (empty ? dim : fg);
        g.drawFittedText (juce::String (slotLetters[s]) + "  " + labels[s], a.reduced (10.0f).toNearestInt(), j, 1);
    };
    corner (0, juce::Justification::topLeft);
    corner (1, juce::Justification::topRight);
    corner (2, juce::Justification::bottomLeft);
    corner (3, juce::Justification::bottomRight);

    // la "luz": un halo alrededor del cursor
    const juce::Point<float> pos (a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight());
    juce::ColourGradient glow (fg.withAlpha (0.35f), pos, fg.withAlpha (0.0f), pos.translated (90.0f, 0.0f), true);
    g.setGradientFill (glow);
    g.fillEllipse (juce::Rectangle<float> (180.0f, 180.0f).withCentre (pos));
    g.setColour (fg);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (pos));
    g.setColour (bg);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pos));
}

void XYPad::setFromMouse (juce::Point<float> m)
{
    const auto a = padArea();
    const float nx = juce::jlimit (0.0f, 1.0f, (m.x - a.getX()) / a.getWidth());
    const float ny = juce::jlimit (0.0f, 1.0f, (a.getBottom() - m.y) / a.getHeight());
    xAttach.setValueAsPartOfGesture (nx);
    yAttach.setValueAsPartOfGesture (ny);
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    xAttach.beginGesture();
    yAttach.beginGesture();
    setFromMouse (e.position);
}

void XYPad::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e.position); }

void XYPad::mouseUp (const juce::MouseEvent&)
{
    xAttach.endGesture();
    yAttach.endGesture();
}

//==============================================================================
void EnvelopeView::paint (juce::Graphics& g)
{
    using namespace Colours_;
    const auto a = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRect (a);
    g.setColour (line);
    g.drawRect (a, 1.0f);

    const float fMin = 80.0f, fMax = 12000.0f;
    auto xForHz = [&] (float hz) { return a.getX() + a.getWidth() * std::log (hz / fMin) / std::log (fMax / fMin); };

    g.setFont (juce::FontOptions (11.0f));
    for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
    {
        const float xx = xForHz (hz);
        g.setColour (line);
        g.drawVerticalLine ((int) xx, a.getY(), a.getBottom());
        g.setColour (dim);
        g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz),
                    (int) xx + 3, (int) a.getBottom() - 16, 40, 14, juce::Justification::left);
    }

    float weight = 0.0f;
    const auto env = proc.getBlendedTargetForDisplay (weight);
    g.setColour (dim);
    g.drawText (u8 ("TIMBRE OBJETIVO"), a.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
    if (weight < 1.0e-4f)
    {
        g.drawText (u8 ("Cargá una voz en un slot para empezar"), a.toNearestInt(), juce::Justification::centred);
        return;
    }

    // autoescala: el pico de la curva queda arriba, se muestran 50 dB hacia abajo
    std::vector<float> dbs;
    float maxDb = -1000.0f;
    const int w = (int) a.getWidth();
    for (int px = 0; px <= w; px += 2)
    {
        const float hz = fMin * std::pow (fMax / fMin, (float) px / a.getWidth());
        const float pos = hz / pvr::kGridMaxHz * (float) (pvr::kGridSize - 1);
        const int i = juce::jlimit (0, pvr::kGridSize - 2, (int) pos);
        const float fr = pos - (float) i;
        dbs.push_back (8.6859f * (env[(size_t) i] + fr * (env[(size_t) i + 1] - env[(size_t) i])));
        maxDb = juce::jmax (maxDb, dbs.back());
    }
    const float top = a.getY() + 26.0f, bottom = a.getBottom() - 20.0f, dbRange = 50.0f;
    juce::Path path;
    for (size_t j = 0; j < dbs.size(); ++j)
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (maxDb - dbs[j]) / dbRange);
        const float xx = a.getX() + (float) (j * 2), yy = top + norm * (bottom - top);
        if (j == 0) path.startNewSubPath (xx, yy); else path.lineTo (xx, yy);
    }
    g.setColour (Colours_::fg.withAlpha (0.4f + 0.6f * juce::jmin (1.0f, weight)));
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

//==============================================================================
PVRVoiceMorphEditor::PVRVoiceMorphEditor (PVRVoiceMorphProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pad (p), envView (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (pad);
    addAndMakeVisible (envView);

    const char* ids[]    = { "amount", "formant", "pitch", "mix", "output" };
    const char* names[]  = { "TIMBRE", "FORMANTES", "PITCH", "MEZCLA", "SALIDA" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setDoubleClickReturnValue (true, (double) p.apvts.getParameterRange (ids[i])
                                                       .convertFrom0to1 (p.apvts.getParameter (ids[i])->getDefaultValue()));
        k.label.setText (names[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids[i], k.slider);
    }

    for (int s = 0; s < PVRVoiceMorphProcessor::numSlots; ++s)
    {
        auto& b = slotButtons[(size_t) s];
        b.onClick = [this, s] { showSlotMenu (s); };
        addAndMakeVisible (b);
    }

    qualityBox.addItemList ({ "Calidad alta", "Baja latencia" }, 1);
    addAndMakeVisible (qualityBox);
    qualityAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "quality", qualityBox);

    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setColour (juce::Label::textColourId, Colours_::dim);
    addAndMakeVisible (latencyLabel);

    for (auto& k : knobs)
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, Colours_::line);

    refreshSlots();
    timerCallback();
    setSize (820, 540);
    startTimerHz (15);
}

PVRVoiceMorphEditor::~PVRVoiceMorphEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PVRVoiceMorphEditor::paint (juce::Graphics& g)
{
    using namespace Colours_;
    g.fillAll (bg);
    g.setColour (fg);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("PVR", 20, 14, 60, 30, juce::Justification::left);
    g.setColour (dim);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("VOICEMORPH", 70, 14, 200, 30, juce::Justification::left);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (u8 ("VOCES"), 360, 58, 200, 14, juce::Justification::left);
    g.setColour (line);
    g.drawHorizontalLine (400, 20.0f, (float) getWidth() - 20.0f);
}

void PVRVoiceMorphEditor::resized()
{
    pad.setBounds (20, 58, 320, 320);
    qualityBox.setBounds (getWidth() - 180, 18, 160, 26);
    latencyLabel.setBounds (getWidth() - 400, 18, 210, 26);

    int y = 76;
    for (auto& b : slotButtons) { b.setBounds (360, y, 440, 32); y += 38; }
    envView.setBounds (360, y + 6, 440, 378 - (y + 6));

    const int kw = (getWidth() - 40) / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int kx = 20 + (int) i * kw;
        knobs[i].label.setBounds (kx, 412, kw, 18);
        knobs[i].slider.setBounds (kx + (kw - 100) / 2, 430, 100, 100);
    }
}

void PVRVoiceMorphEditor::timerCallback()
{
    if (proc.getSlotsVersion() != lastSlotsVersion) refreshSlots();
    const double ms = 1000.0 * proc.getLatencySamples() / juce::jmax (1.0, proc.getSampleRate());
    latencyLabel.setText ("Latencia " + juce::String (juce::roundToInt (ms)) + " ms", juce::dontSendNotification);
    envView.repaint();
}

void PVRVoiceMorphEditor::refreshSlots()
{
    lastSlotsVersion = proc.getSlotsVersion();
    for (int s = 0; s < PVRVoiceMorphProcessor::numSlots; ++s)
    {
        const auto prof = proc.getSlot (s);
        juce::String txt = juce::String (slotLetters[s]) + "   " + proc.getSlotLabel (s);
        if (prof.isValid() && prof.seconds > 0.0f)
            txt << u8 ("   ·  ") << juce::String (prof.seconds, 1) << " s";
        if (prof.isValid() && prof.medianF0 > 0.0f)
            txt << u8 ("  ·  ~") << juce::String ((int) prof.medianF0) << " Hz";
        slotButtons[(size_t) s].setButtonText (txt);
    }
    pad.refreshLabels();
}

void PVRVoiceMorphEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, title, text, "OK", this);
}

void PVRVoiceMorphEditor::showSlotMenu (int slot)
{
    const auto prof = proc.getSlot (slot);
    juce::PopupMenu m;
    m.addSectionHeader (u8 ("Slot ") + slotLetters[slot]);
    m.addItem (1, u8 ("Cargar muestra de voz (WAV / AIFF / FLAC)..."));
    juce::PopupMenu gen;
    gen.addItem (10, u8 ("Voz grave"));
    gen.addItem (11, u8 ("Voz media"));
    gen.addItem (12, u8 ("Voz aguda"));
    m.addSubMenu (u8 ("Generar voz aleatoria"), gen);
    m.addItem (2, u8 ("Pegar código de voz..."));
    const bool isGenerated = prof.isValid() && juce::String (prof.name).startsWith ("PVR-");
    m.addItem (3, u8 ("Copiar código de voz"), isGenerated);
    m.addSeparator();
    m.addItem (4, u8 ("Abrir voz guardada (.pvrvoice)..."));
    m.addItem (5, u8 ("Guardar esta voz (.pvrvoice)..."), prof.isValid());
    m.addSeparator();
    m.addItem (6, u8 ("Vaciar slot"), prof.isValid());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[(size_t) slot]),
        [this, slot, name = prof.name] (int r)
        {
            switch (r)
            {
                case 1: chooseSample (slot); break;
                case 2: askForCode (slot); break;
                case 3: juce::SystemClipboard::copyTextToClipboard (juce::String (name)); break;
                case 4: openVoiceFile (slot); break;
                case 5: saveVoiceFile (slot); break;
                case 6: proc.clearSlot (slot); break;
                case 10: case 11: case 12:
                {
                    auto& rnd = juce::Random::getSystemRandom();
                    const float gender = 0.15f + 0.35f * (float) (r - 10) + 0.1f * (rnd.nextFloat() - 0.5f);
                    proc.generateIntoSlot (slot, gender, rnd.nextFloat(), (uint32_t) rnd.nextInt (65536));
                    break;
                }
                default: break;
            }
        });
}

void PVRVoiceMorphEditor::chooseSample (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (u8 ("Elegí una muestra de voz (5-30 s, voz seca)"),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const auto err = proc.loadSampleIntoSlot (slot, f);
            if (err.isNotEmpty()) showMessage (u8 ("No se pudo cargar la voz"), err);
        });
}

void PVRVoiceMorphEditor::openVoiceFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (u8 ("Abrir voz"), juce::File(), "*.pvrvoice");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const auto err = proc.loadSlotFromFile (slot, f);
            if (err.isNotEmpty()) showMessage (u8 ("No se pudo abrir"), err);
        });
}

void PVRVoiceMorphEditor::saveVoiceFile (int slot)
{
    const auto prof = proc.getSlot (slot);
    const auto def = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (prof.name.c_str())) + ".pvrvoice");
    chooser = std::make_unique<juce::FileChooser> (u8 ("Guardar voz"), def, "*.pvrvoice");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, slot] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (! f.hasFileExtension ("pvrvoice")) f = f.withFileExtension ("pvrvoice");
            if (! proc.saveSlotToFile (slot, f)) showMessage (u8 ("No se pudo guardar"), f.getFullPathName());
        });
}

void PVRVoiceMorphEditor::askForCode (int slot)
{
    auto* w = new juce::AlertWindow (u8 ("Código de voz"),
                                     u8 ("Pegá un código del tipo PVR-1A2B-80-C4"),
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("code", juce::SystemClipboard::getTextFromClipboard().trim().substring (0, 20), u8 ("Código"));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, slot] (int r)
    {
        if (r == 1 && ! proc.loadVoiceCodeIntoSlot (slot, w->getTextEditorContents ("code")))
            showMessage (u8 ("Código inválido"), u8 ("El formato es PVR-XXXX-XX-XX."));
    }), true);
}

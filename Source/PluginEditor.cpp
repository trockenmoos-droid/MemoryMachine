#include "PluginEditor.h"

using namespace juce;

// ============================================================================
mmui::LookAndFeel::LookAndFeel()
{
    setColour (Slider::textBoxTextColourId,      text);
    setColour (Slider::textBoxOutlineColourId,   Colours::transparentBlack);
    setColour (Label::textColourId,              text);
    setColour (ComboBox::backgroundColourId,     panel.brighter (0.05f));
    setColour (ComboBox::outlineColourId,        edge);
    setColour (ComboBox::textColourId,           text);
    setColour (ComboBox::arrowColourId,          lichen);
    setColour (PopupMenu::backgroundColourId,    panel);
    setColour (PopupMenu::textColourId,          text);
    setColour (PopupMenu::highlightedBackgroundColourId, moss);
    setColour (PopupMenu::highlightedTextColourId, text);
}

void mmui::LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float r  = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c  = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;

    // body: a soft, slightly lumpy disc like a cushion of moss
    g.setColour (panel.brighter (0.12f).withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
    g.setColour (edge.withAlpha (alpha));
    g.drawEllipse (Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c), 1.0f);

    // track + value arc
    Path track; track.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0.0f, start, end, true);
    g.setColour (edge.withAlpha (alpha));
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val; val.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0.0f, start, angle, true);
    g.setColour (lichen.withAlpha (alpha));
    g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // pointer
    const Point<float> tip (c.x + std::sin (angle) * r * 0.62f, c.y - std::cos (angle) * r * 0.62f);
    const Point<float> base (c.x + std::sin (angle) * r * 0.2f, c.y - std::cos (angle) * r * 0.2f);
    g.setColour (text.withAlpha (alpha));
    g.drawLine ({ base, tip }, 2.0f);
}

void mmui::LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? moss : panel.brighter (0.05f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? lichen : (over ? dim : edge));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? text : dim);
    g.setFont (FontOptions (13.0f));
    g.drawText (b.getButtonText(), r, Justification::centred);
}

// ============================================================================
MemoryMachineEditor::MemoryMachineEditor (MemoryMachineProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    fragment = &makeKnob ("fragment",   "Fragment");
    stutter  = &makeKnob ("stutterMs",  "Slice");
    memory   = &makeKnob ("memorySec",  "Memory");
    jumpP    = &makeKnob ("jumpProb",   "Deja-vu");
    jumpD    = &makeKnob ("jumpDepth",  "How far back");
    blur     = &makeKnob ("blur",       "Blur");
    bits     = &makeKnob ("bits",       "Bits");
    down     = &makeKnob ("downsample", "Downsample");
    forget   = &makeKnob ("forget",     "Forget");
    driftD   = &makeKnob ("driftDepth", "Depth");
    driftR   = &makeKnob ("driftRate",  "Rate");
    mix      = &makeKnob ("mix",        "Mix");
    out      = &makeKnob ("outDB",      "Output");

    addAndMakeVisible (sync);
    syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "stutSync", sync);
    sync.onStateChange = [this] { updateSyncState(); };

    if (auto* choice = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("stutNote")))
        note.addItemList (choice->choices, 1);
    addAndMakeVisible (note);
    noteAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "stutNote", note);
    noteLabel.setText ("Note", dontSendNotification);
    noteLabel.setJustificationType (Justification::centred);
    noteLabel.setFont (FontOptions (12.0f));
    addAndMakeVisible (noteLabel);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presets.addItem (proc.getProgramName (i), i + 1);
    presets.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    presets.onChange = [this] { proc.setCurrentProgram (presets.getSelectedId() - 1); };
    addAndMakeVisible (presets);

    updateSyncState();
    setSize (780, 440);
    startTimerHz (30);
}

MemoryMachineEditor::~MemoryMachineEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

MemoryMachineEditor::Knob& MemoryMachineEditor::makeKnob (const String& id, const String& name)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    k->slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k->slider.setColour (Slider::textBoxTextColourId, mmui::text);
    k->slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    k->slider.setColour (Slider::textBoxHighlightColourId, mmui::moss);
    k->label.setText (name, dontSendNotification);
    k->label.setJustificationType (Justification::centred);
    k->label.setFont (FontOptions (12.5f));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    jassert (proc.apvts.getParameter (id) != nullptr);
    k->att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void MemoryMachineEditor::updateSyncState()
{
    const bool s = sync.getToggleState();
    stutter->slider.setEnabled (! s);
    stutter->label.setAlpha (s ? 0.4f : 1.0f);
    note.setEnabled (s);
    noteLabel.setAlpha (s ? 1.0f : 0.4f);
}

void MemoryMachineEditor::timerCallback()
{
    const float target = proc.recalling.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float next = led + (target - led) * 0.35f;
    if (std::abs (next - led) > 0.002f) { led = next; repaint (ledArea); }
    if (presets.getSelectedId() != proc.getCurrentProgram() + 1)
        presets.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

void MemoryMachineEditor::paint (Graphics& g)
{
    g.fillAll (mmui::bg);

    // faint contour lines in the background, like an old map of the Salzkammergut
    Random rnd (51);
    g.setColour (mmui::moss.withAlpha (0.08f));
    for (int i = 0; i < 9; ++i)
    {
        Path p;
        const float y0 = 30.0f + i * 48.0f;
        p.startNewSubPath (0, y0);
        for (int x = 0; x <= getWidth(); x += 40)
            p.lineTo ((float) x, y0 + 12.0f * std::sin (x * 0.012f + i * 0.9f) + rnd.nextFloat() * 4.0f);
        g.strokePath (p, PathStrokeType (1.0f));
    }

    // title
    g.setColour (mmui::text);
    g.setFont (FontOptions (24.0f).withStyle ("Bold"));
    g.drawText ("MEMORY MACHINE", 20, 12, 300, 30, Justification::centredLeft);
    g.setColour (mmui::dim);
    g.setFont (FontOptions (11.5f));
    g.drawText ("trockenmoos audio", 22, 40, 300, 14, Justification::centredLeft);

    auto section = [&] (Rectangle<int> r, const String& name)
    {
        g.setColour (mmui::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (mmui::edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (mmui::lichen);
        g.setFont (FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (name, r.getX() + 12, r.getY() + 6, 200, 14, Justification::centredLeft);
    };
    section (recallArea, "RECALL");
    section (decayArea,  "DECAY");
    section (driftArea,  "DRIFT");
    section (outArea,    "OUT");

    // recall indicator
    const auto l = ledArea.toFloat();
    g.setColour (mmui::lichen.withAlpha (0.12f + 0.25f * led));
    g.fillEllipse (l.expanded (3.0f * led));
    g.setColour (mmui::edge.interpolatedWith (mmui::lichen, led));
    g.fillEllipse (l.reduced (2.0f));
}

void MemoryMachineEditor::layoutKnobs (Rectangle<int> area, std::initializer_list<Knob*> ks)
{
    const int w = area.getWidth() / (int) ks.size();
    for (auto* k : ks)
    {
        auto cell = area.removeFromLeft (w).reduced (4, 0);
        k->label.setBounds (cell.removeFromBottom (18));
        k->slider.setBounds (cell);
    }
}

void MemoryMachineEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    presets.setBounds (header.removeFromRight (190).withSizeKeepingCentre (190, 26));
    ledArea = header.removeFromRight (36).withSizeKeepingCentre (12, 12);

    r.removeFromTop (6);
    recallArea = r.removeFromTop (r.getHeight() / 2 - 4);
    r.removeFromTop (8);
    const int unit = r.getWidth() / 8;
    decayArea = r.removeFromLeft (unit * 4 - 4);
    r.removeFromLeft (8);
    driftArea = r.removeFromLeft (unit * 2 - 4);
    r.removeFromLeft (8);
    outArea = r;

    auto inner = [] (Rectangle<int> a) { return a.reduced (8).withTrimmedTop (16); };

    auto rec = inner (recallArea);
    auto syncCol = rec.removeFromRight (100).reduced (6, 10);
    sync.setBounds (syncCol.removeFromTop (28));
    syncCol.removeFromTop (12);
    note.setBounds (syncCol.removeFromTop (26));
    noteLabel.setBounds (syncCol.removeFromTop (18));
    layoutKnobs (rec, { fragment, stutter, memory, jumpP, jumpD });

    layoutKnobs (inner (decayArea), { blur, bits, down, forget });
    layoutKnobs (inner (driftArea), { driftD, driftR });
    layoutKnobs (inner (outArea),   { mix, out });
}

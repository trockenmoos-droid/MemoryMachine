#include "PluginEditor.h"

using namespace juce;

namespace { constexpr int kUserIdBase = 1000; }

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
    setColour (PopupMenu::headerTextColourId,    lichen);
    setColour (PopupMenu::highlightedBackgroundColourId, moss);
    setColour (PopupMenu::highlightedTextColourId, text);
    setColour (TextButton::textColourOffId,      text);
    setColour (TextButton::textColourOnId,       text);
    setColour (TextEditor::backgroundColourId,   panel.brighter (0.08f));
    setColour (TextEditor::textColourId,         text);
    setColour (TextEditor::outlineColourId,      edge);
    setColour (TextEditor::focusedOutlineColourId, lichen);
    setColour (AlertWindow::backgroundColourId,  panel);
    setColour (AlertWindow::textColourId,        text);
    setColour (AlertWindow::outlineColourId,     edge);
}

void mmui::LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float r  = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c  = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto  accent = s.findColour (Slider::rotarySliderFillColourId);

    g.setColour (panel.brighter (0.12f).withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
    g.setColour (edge.withAlpha (alpha));
    g.drawEllipse (Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c), 1.0f);

    Path track; track.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0.0f, start, end, true);
    g.setColour (edge.withAlpha (alpha));
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val; val.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0.0f, start, angle, true);
    g.setColour (accent.withAlpha (alpha));
    g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

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

void mmui::LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? moss : panel.brighter (over ? 0.12f : 0.05f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (over ? lichen.withAlpha (0.7f) : edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
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
    fFreq    = &makeKnob ("filterHz",   "Freq");
    fReso    = &makeKnob ("filterReso", "Resonance");
    fDepth   = &makeKnob ("sweepDepth", "Sweep");
    fRate    = &makeKnob ("sweepRate",  "Speed");
    reverse    = &makeKnob ("reverse",    "Backwards",  mmui::rust);
    warp       = &makeKnob ("warp",       "Warp",       mmui::rust);
    rumination = &makeKnob ("rumination", "Rumination", mmui::rust);

    addAndMakeVisible (sync);
    syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "stutSync", sync);
    sync.onStateChange = [this] { updateEnablement(); };

    makeChoice (note,       "stutNote",   "Note");
    makeChoice (filterType, "filterType", "Type");
    makeChoice (sweepShape, "sweepShape", "Motion");
    filterType.box.onChange = [this] { updateEnablement(); };
    sweepShape.box.onChange = [this] { updateEnablement(); };

    // preset bar
    for (auto* b : { &prevButton, &nextButton, &saveButton, &menuButton })
        addAndMakeVisible (*b);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    saveButton.onClick = [this] { askToSave(); };
    menuButton.onClick = [this] { showPresetMenu(); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current settings as a user preset");

    presets.onChange = [this] { selectPresetById (presets.getSelectedId()); };
    addAndMakeVisible (presets);
    rebuildPresetList();

    updateEnablement();
    setSize (820, 640);
    startTimerHz (30);
}

MemoryMachineEditor::~MemoryMachineEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

MemoryMachineEditor::Knob& MemoryMachineEditor::makeKnob (const String& id, const String& name, Colour accent)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    k->slider.setColour (Slider::rotarySliderFillColourId, accent);
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

void MemoryMachineEditor::makeChoice (Choice& c, const String& id, const String& name)
{
    if (auto* choice = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c.box.addItemList (choice->choices, 1);
    c.label.setText (name, dontSendNotification);
    c.label.setJustificationType (Justification::centred);
    c.label.setFont (FontOptions (12.0f));
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c.box);
}

void MemoryMachineEditor::updateEnablement()
{
    const bool s = sync.getToggleState();
    stutter->slider.setEnabled (! s);
    stutter->label.setAlpha (s ? 0.4f : 1.0f);
    note.box.setEnabled (s);
    note.label.setAlpha (s ? 1.0f : 0.4f);

    const bool fOn = filterType.box.getSelectedItemIndex() > 0;
    const bool envelope = sweepShape.box.getSelectedItemIndex() == 3;
    for (auto* k : { fFreq, fReso, fDepth, fRate })
    {
        const bool en = fOn && ! (k == fRate && envelope);
        k->slider.setEnabled (en);
        k->label.setAlpha (en ? 1.0f : 0.4f);
    }
    sweepShape.box.setEnabled (fOn);
    sweepShape.label.setAlpha (fOn ? 1.0f : 0.4f);
}

// ---- presets ------------------------------------------------------------------
void MemoryMachineEditor::rebuildPresetList()
{
    userNames = proc.getUserPresetNames();
    presets.clear (dontSendNotification);
    presets.addSectionHeading ("Factory");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presets.addItem (proc.getProgramName (i), i + 1);
    if (! userNames.isEmpty())
    {
        presets.addSeparator();
        presets.addSectionHeading ("User");
        for (int i = 0; i < userNames.size(); ++i)
            presets.addItem (userNames[i], kUserIdBase + i);
    }
    shownPreset = {};
    timerCallback(); // sync the shown name
}

std::vector<int> MemoryMachineEditor::presetIds() const
{
    std::vector<int> ids;
    for (int i = 0; i < proc.getNumPrograms(); ++i) ids.push_back (i + 1);
    for (int i = 0; i < userNames.size(); ++i) ids.push_back (kUserIdBase + i);
    return ids;
}

void MemoryMachineEditor::selectPresetById (int id)
{
    if (id <= 0) return;
    if (id >= kUserIdBase)
    {
        const auto name = userNames[id - kUserIdBase];
        if (! proc.loadUserPreset (name))
        {
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                        .withTitle ("Preset").withMessage ("Could not load \"" + name + "\".")
                                        .withButton ("OK"), nullptr);
            rebuildPresetList();
            return;
        }
    }
    else
        proc.setCurrentProgram (id - 1);
    shownPreset = {};
}

void MemoryMachineEditor::stepPreset (int delta)
{
    const auto ids = presetIds();
    if (ids.empty()) return;
    const int cur = presets.getSelectedId();
    auto it = std::find (ids.begin(), ids.end(), cur);
    int idx = it == ids.end() ? 0 : (int) std::distance (ids.begin(), it) + delta;
    idx = (idx + (int) ids.size()) % (int) ids.size();
    presets.setSelectedId (ids[(size_t) idx], sendNotificationSync);
}

void MemoryMachineEditor::askToSave()
{
    auto* w = new AlertWindow ("Save preset", "Name for this preset:", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.isUserPreset() ? proc.getPresetName() : String(), {});
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    SafePointer<MemoryMachineEditor> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty()) return;

        auto doSave = [safe, name]
        {
            if (safe == nullptr) return;
            if (! safe->proc.saveUserPreset (name))
                AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                            .withTitle ("Preset").withMessage ("Could not save to\n"
                                                + safe->proc.getPresetFolder().getFullPathName())
                                            .withButton ("OK"), nullptr);
            safe->rebuildPresetList();
        };

        if (safe->proc.getUserPresetNames().contains (name))
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                        .withTitle ("Overwrite?")
                                        .withMessage ("A preset called \"" + name + "\" already exists.")
                                        .withButton ("Overwrite").withButton ("Cancel"),
                                    [doSave] (int r) { if (r == 1) doSave(); });
        else
            doSave();
    }), true);
}

void MemoryMachineEditor::showPresetMenu()
{
    PopupMenu m;
    m.addItem (1, "Save as...");
    const bool canDelete = proc.isUserPreset() && userNames.contains (proc.getPresetName());
    m.addItem (2, "Delete \"" + proc.getPresetName() + "\"", canDelete);
    m.addSeparator();
    m.addItem (3, "Open preset folder");
    m.addItem (4, "Rescan presets");

    SafePointer<MemoryMachineEditor> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&menuButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->askToSave();
        if (r == 2)
        {
            const auto name = safe->proc.getPresetName();
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                        .withTitle ("Delete preset?")
                                        .withMessage ("\"" + name + "\" is moved to the trash.")
                                        .withButton ("Delete").withButton ("Cancel"),
                                    [safe, name] (int res)
                                    {
                                        if (res == 1 && safe != nullptr)
                                        {
                                            safe->proc.deleteUserPreset (name);
                                            safe->rebuildPresetList();
                                        }
                                    });
        }
        if (r == 3)
        {
            auto dir = safe->proc.getPresetFolder();
            dir.createDirectory();
            dir.revealToUser();
        }
        if (r == 4) safe->rebuildPresetList();
    });
}

// ---- timer / paint / layout ---------------------------------------------------------
void MemoryMachineEditor::timerCallback()
{
    const float target = proc.recalling.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float next = led + (target - led) * 0.35f;
    if (std::abs (next - led) > 0.002f) { led = next; repaint (ledArea); }

    // keep the preset bar in step with the processor (host program changes, session load)
    const auto name = proc.getPresetName();
    const auto key = name + (proc.isUserPreset() ? "|u" : "|f");
    if (key != shownPreset)
    {
        shownPreset = key;
        int id = 0;
        if (proc.isUserPreset()) { const int i = userNames.indexOf (name); if (i >= 0) id = kUserIdBase + i; }
        else id = proc.getCurrentProgram() + 1;
        if (id > 0) presets.setSelectedId (id, dontSendNotification);
        else        presets.setText (name, dontSendNotification);
    }
}

void MemoryMachineEditor::paint (Graphics& g)
{
    g.fillAll (mmui::bg);

    // faint contour lines in the background, like an old map of the Salzkammergut
    Random rnd (51);
    g.setColour (mmui::moss.withAlpha (0.08f));
    for (int i = 0; i < 13; ++i)
    {
        Path p;
        const float y0 = 30.0f + i * 48.0f;
        p.startNewSubPath (0, y0);
        for (int x = 0; x <= getWidth(); x += 40)
            p.lineTo ((float) x, y0 + 12.0f * std::sin (x * 0.012f + i * 0.9f) + rnd.nextFloat() * 4.0f);
        g.strokePath (p, PathStrokeType (1.0f));
    }

    g.setColour (mmui::text);
    g.setFont (FontOptions (24.0f).withStyle ("Bold"));
    g.drawText ("MEMORY MACHINE", 20, 12, 300, 30, Justification::centredLeft);
    g.setColour (mmui::dim);
    g.setFont (FontOptions (11.5f));
    g.drawText (String::fromUTF8 ("trockenmoos audio  \xc2\xb7  v") + JucePlugin_VersionString,
                22, 40, 300, 14, Justification::centredLeft);

    auto section = [&] (Rectangle<int> r, const String& name, Colour accent)
    {
        g.setColour (mmui::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (mmui::edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (accent);
        g.setFont (FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (name, r.getX() + 12, r.getY() + 6, 200, 14, Justification::centredLeft);
    };
    section (recallArea, "RECALL",        mmui::lichen);
    section (decayArea,  "DECAY",         mmui::lichen);
    section (driftArea,  "DRIFT",         mmui::lichen);
    section (outArea,    "OUT",           mmui::lichen);
    section (filterArea, "MOVING FILTER", mmui::lichen);
    section (weirdArea,  "WEIRD",         mmui::rust);

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

void MemoryMachineEditor::layoutChoice (Choice& c, Rectangle<int> area)
{
    c.box.setBounds (area.removeFromTop (26));
    c.label.setBounds (area.removeFromTop (18));
}

void MemoryMachineEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    auto bar = header.removeFromRight (390).withSizeKeepingCentre (390, 26);
    menuButton.setBounds (bar.removeFromRight (34));  bar.removeFromRight (4);
    saveButton.setBounds (bar.removeFromRight (56));  bar.removeFromRight (6);
    nextButton.setBounds (bar.removeFromRight (26));  bar.removeFromRight (2);
    prevButton.setBounds (bar.removeFromLeft (26));   bar.removeFromLeft (2);
    presets.setBounds (bar);
    ledArea = header.removeFromRight (36).withSizeKeepingCentre (12, 12);

    r.removeFromTop (6);
    const int rowH = (r.getHeight() - 16) / 3;
    recallArea = r.removeFromTop (rowH);
    r.removeFromTop (8);
    auto row2 = r.removeFromTop (rowH);
    r.removeFromTop (8);
    auto row3 = r;

    const int unit = row2.getWidth() / 8;
    decayArea = row2.removeFromLeft (unit * 4 - 4);  row2.removeFromLeft (8);
    driftArea = row2.removeFromLeft (unit * 2 - 4);  row2.removeFromLeft (8);
    outArea   = row2;

    filterArea = row3.removeFromLeft (unit * 5 - 4); row3.removeFromLeft (8);
    weirdArea  = row3;

    auto inner = [] (Rectangle<int> a) { return a.reduced (8).withTrimmedTop (16); };

    auto rec = inner (recallArea);
    auto syncCol = rec.removeFromRight (100).reduced (6, 10);
    sync.setBounds (syncCol.removeFromTop (28));
    syncCol.removeFromTop (12);
    layoutChoice (note, syncCol);
    layoutKnobs (rec, { fragment, stutter, memory, jumpP, jumpD });

    layoutKnobs (inner (decayArea), { blur, bits, down, forget });
    layoutKnobs (inner (driftArea), { driftD, driftR });
    layoutKnobs (inner (outArea),   { mix, out });

    auto fil = inner (filterArea);
    auto choiceCol = fil.removeFromLeft (110).reduced (6, 8);
    layoutChoice (filterType, choiceCol.removeFromTop (48));
    choiceCol.removeFromTop (8);
    layoutChoice (sweepShape, choiceCol.removeFromTop (48));
    layoutKnobs (fil, { fFreq, fReso, fDepth, fRate });

    layoutKnobs (inner (weirdArea), { reverse, warp, rumination });
}

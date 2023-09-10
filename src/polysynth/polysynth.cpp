/*
 * Conduit - a series of demonstration and fun plugins
 *
 * Copyright 2023 Paul Walker and authors in github
 *
 * This file you are viewing now is released under the
 * MIT license, but the assembled program which results
 * from compiling it has GPL3 dependencies, so the total
 * program is a GPL3 program. More details to come.
 *
 * Basically before I give this to folks, document this bit and
 * replace these headers
 *
 */

#include "polysynth.h"
#include <iostream>
#include <cmath>
#include <cstring>

#include <iomanip>
#include <locale>

#include "version.h"


namespace sst::conduit::polysynth
{

const char *features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
clap_plugin_descriptor desc = {CLAP_VERSION,
                               "org.surge-synth-team.conduit.polysynth",
                               "Conduit Polysynth",
                               "Surge Synth Team",
                               "https://surge-synth-team.org",
                               "",
                               "",
                               sst::conduit::build::FullVersionStr,
                               "The Conduit Polysynth is a work in progress",
                               features};

ConduitPolysynth::ConduitPolysynth(const clap_host *host)
    : sst::conduit::shared::ClapBaseClass<ConduitPolysynth, ConduitPolysynthConfig>(&desc, host)
{
    auto autoFlag = CLAP_PARAM_IS_AUTOMATABLE;
    auto modFlag = autoFlag | CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID |
                   CLAP_PARAM_IS_MODULATABLE_PER_KEY;
    auto steppedFlag = autoFlag | CLAP_PARAM_IS_STEPPED;

    paramDescriptions.push_back(ParamDesc()
                                    .asInt()
                                    .withID(pmUnisonCount)
                                    .withName("Unison Count")
                                    .withGroupName("Oscillator")
                                    .withRange(1, SawDemoVoice::max_uni)
                                    .withDefault(3)
                                    .withFlags(steppedFlag)
                                    .withLinearScaleFormatting("voices"));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmUnisonSpread)
                                    .withName("Unison Spread")
                                    .withGroupName("Oscillator")
                                    .withLinearScaleFormatting("cents")
                                    .withRange(0, 100)
                                    .withDefault(10)
                                    .withFlags(modFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmOscDetune)
                                    .withName("Unison Detune")
                                    .withGroupName("Oscillator")
                                    .withLinearScaleFormatting("cents")
                                    .withRange(-200, 200)
                                    .withDefault(0)
                                    .withFlags(modFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmAmpAttack)
                                    .withName("Amplitude Attack")
                                    .withGroupName("AEG")
                                    .withLinearScaleFormatting("seconds")
                                    .withRange(0, 1)
                                    .withDefault(0.05)
                                    .withFlags(autoFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmAmpRelease)
                                    .withName("Amplitude Release")
                                    .withGroupName("AEG")
                                    .withLinearScaleFormatting("seconds")
                                    .withRange(0, 1)
                                    .withDefault(0.05)
                                    .withFlags(autoFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asBool()
                                    .withID(pmAmpIsGate)
                                    .withName("Bypass Amp Envelope")
                                    .withGroupName("AEG")
                                    .withFlags(steppedFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmCutoff)
                                    .withName("Cutoff")
                                    .withGroupName("Filter")
                                    .withRange(1, 127)
                                    .withDefault(69)
                                    .withSemitoneZeroAtMIDIZeroFormatting()
                                    .withFlags(modFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmResonance)
                                    .withName("Resonance")
                                    .withGroupName("Filter")
                                    .withRange(0, 1)
                                    .withDefault(sqrt(2) / 2)
                                    .withLinearScaleFormatting("")
                                    .withFlags(modFlag));
    paramDescriptions.push_back(ParamDesc()
                                    .asFloat()
                                    .withID(pmPreFilterVCA)
                                    .withName("PreFilter VCA")
                                    .withGroupName("Filter")
                                    .withRange(0, 1)
                                    .withDefault(1)
                                    .withLinearScaleFormatting("")
                                    .withFlags(modFlag));

    std::unordered_map<int, std::string> filterModes;
    using sv = SawDemoVoice::StereoSimperSVF;
    filterModes[sv::LP] = "Low Pass";
    filterModes[sv::HP] = "High Pass";
    filterModes[sv::BP] = "Band Pass";
    filterModes[sv::NOTCH] = "Notch";
    filterModes[sv::PEAK] = "Peak";
    filterModes[sv::ALL] = "All Pass";
    paramDescriptions.push_back(
        ParamDesc()
            .asInt()
            .withID(pmFilterMode)
            .withName("Filter Type")
            .withGroupName("Filter")
            .withRange(SawDemoVoice::StereoSimperSVF::LP, SawDemoVoice::StereoSimperSVF::ALL)
            .withUnorderedMapFormatting(filterModes)
            .withFlags(steppedFlag));

    configureParams();

    attachParam(pmUnisonCount, unisonCount);
    attachParam(pmUnisonSpread, unisonSpread);
    attachParam(pmOscDetune, oscDetune);
    attachParam(pmCutoff, cutoff);
    attachParam(pmResonance, resonance);
    attachParam(pmAmpAttack, ampAttack);
    attachParam(pmAmpRelease, ampRelease);
    attachParam(pmAmpIsGate, ampIsGate);
    attachParam(pmPreFilterVCA, preFilterVCA);
    attachParam(pmFilterMode, filterMode);

    terminatedVoices.reserve(max_voices * 4);

    clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
    clapJuceShim->setResizable(true);
}
ConduitPolysynth::~ConduitPolysynth()
{
    // I *think* this is a bitwig bug that they won't call guiDestroy if destroying a plugin
    // with an open window but
    if (clapJuceShim)
        guiDestroy();
}

/*
 * Stereo out, Midi in, in a pretty obvious way.
 * The only trick is the idi in also has NOTE_DIALECT_CLAP which provides us
 * with options on note expression and the like.
 */
bool ConduitPolysynth::audioPortsInfo(uint32_t index, bool isInput,
                                      clap_audio_port_info *info) const noexcept
{
    if (isInput || index != 0)
        return false;

    info->id = 0;
    info->in_place_pair = CLAP_INVALID_ID;
    strncpy(info->name, "main", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;

    return true;
}

bool ConduitPolysynth::notePortsInfo(uint32_t index, bool isInput,
                                     clap_note_port_info *info) const noexcept
{
    if (isInput)
    {
        info->id = 1;
        info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        strncpy(info->name, "NoteInput", CLAP_NAME_SIZE);
        return true;
    }
    return false;
}

/*
 * The process function is the heart of any CLAP. It reads inbound events,
 * generates audio if appropriate, writes outbound events, and informs the host
 * to continue operating.
 *
 * In the ConduitPolysynth, our process loop has 3 basic stages
 *
 * 1. See if the UI has sent us any events on the thread-safe UI Queue (
 *    see the discussion in the clap header file for this structure), apply them
 *    to my internal state, and generate CLAP changed messages
 *
 * 2. Iterate over samples rendering the voices, and if an inbound event is coincident
 *    with a sample, process that event for note on, modulation, parameter automation, and so on
 *
 * 3. Detect any voices which have terminated in the block (their state has become 'NEWLY_OFF'),
 *    update them to 'OFF' and send a CLAP NOTE_END event to terminate any polyphonic modulators.
 */
clap_process_status ConduitPolysynth::process(const clap_process *process) noexcept
{
    // If I have no outputs, do nothing
    if (process->audio_outputs_count <= 0)
        return CLAP_PROCESS_SLEEP;

    /*
     * Stage 1:
     *
     * The UI can send us gesture begin/end events which translate in to a
     * `clap_event_param_gesture` or value adjustments.
     */
    auto ct = handleEventsFromUIQueue(process->out_events);
    if (ct)
        pushParamsToVoices();

    /*
     * Stage 2: Create the AUDIO output and process events
     *
     * CLAP has a single inbound event loop where every event is time stamped with
     * a sample id. This means the process loop can easily interleave note and parameter
     * and other events with audio generation. Here we do everything completely sample accurately
     * by maintaining a pointer to the 'nextEvent' which we check at every sample.
     */
    float **out = process->audio_outputs[0].data32;
    auto chans = process->audio_outputs->channel_count;

    auto ev = process->in_events;
    auto sz = ev->size(ev);

    // This pointer is the sentinel to our next event which we advance once an event is processed
    const clap_event_header_t *nextEvent{nullptr};
    uint32_t nextEventIndex{0};
    if (sz != 0)
    {
        nextEvent = ev->get(ev, nextEventIndex);
    }

    for (int i = 0; i < process->frames_count; ++i)
    {
        // Do I have an event to process. Note that multiple events
        // can occur on the same sample, hence 'while' not 'if'
        while (nextEvent && nextEvent->time == i)
        {
            // handleInboundEvent is a separate function which adjusts the state based
            // on event type. We segregate it for clarity but you really should read it!
            handleInboundEvent(nextEvent);
            nextEventIndex++;
            if (nextEventIndex >= sz)
                nextEvent = nullptr;
            else
                nextEvent = ev->get(ev, nextEventIndex);
        }

        // This is a simple accumulator of output across our active voices.
        // See saw-voice.h for information on the individual voice.
        for (int ch = 0; ch < chans; ++ch)
        {
            out[ch][i] = 0.f;
        }
        for (auto &v : voices)
        {
            if (v.isPlaying())
            {
                v.step();
                if (chans >= 2)
                {
                    out[0][i] += v.L;
                    out[1][i] += v.R;
                }
                else if (chans == 1)
                {
                    out[0][i] += (v.L + v.R) * 0.5;
                }
            }
        }
    }

    /*
     * Stage 3 is to inform the host of our terminated voices.
     *
     * This allows hosts which support polyphonic modulation to terminate those
     * modulators, and it is also the reason we have the NEWLY_OFF state in addition
     * to the OFF state.
     *
     * Note that there are two ways to enter the terminatedVoices array. The first
     * is here through natural state transition to NEWLY_OFF and the second is in
     * handleNoteOn when we steal a voice.
     */
    for (auto &v : voices)
    {
        if (v.state == SawDemoVoice::NEWLY_OFF)
        {
            terminatedVoices.emplace_back(v.portid, v.channel, v.key, v.note_id);
            v.state = SawDemoVoice::OFF;
        }
    }

    for (const auto &[portid, channel, key, note_id] : terminatedVoices)
    {
        auto ov = process->out_events;
        auto evt = clap_event_note();
        evt.header.size = sizeof(clap_event_note);
        evt.header.type = (uint16_t)CLAP_EVENT_NOTE_END;
        evt.header.time = process->frames_count - 1;
        evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        evt.header.flags = 0;

        evt.port_index = portid;
        evt.channel = channel;
        evt.key = key;
        evt.note_id = note_id;
        evt.velocity = 0.0;

        ov->try_push(ov, &(evt.header));

        uiComms.dataCopyForUI.updateCount++;
        uiComms.dataCopyForUI.polyphony--;
    }
    terminatedVoices.clear();

    // We should have gotten all the events
    assert(!nextEvent);

    // A little optimization - if we have any active voices continue
    for (const auto &v : voices)
    {
        if (v.state != SawDemoVoice::OFF)
        {
            return CLAP_PROCESS_CONTINUE;
        }
    }

    // Otherwise we have no voices - we can return CLAP_PROCESS_SLEEP until we get the next event
    // And our host can optionally skip processing
    return CLAP_PROCESS_SLEEP;
}

/*
 * handleInboundEvent provides the core event mechanism including
 * voice activation and deactivation, parameter modulation, note expression,
 * and so on.
 *
 * It reads, unsurprisingly, as a simple switch over type with reactions.
 */
void ConduitPolysynth::handleInboundEvent(const clap_event_header_t *evt)
{
    if (handleParamBaseEvents(evt))
    {
        pushParamsToVoices();
        return;
    }

    if (evt->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    switch (evt->type)
    {
    case CLAP_EVENT_MIDI:
    {
        /*
         * We advertise both CLAP_DIALECT_MIDI and CLAP_DIALECT_CLAP_NOTE so we do need
         * to handle midi events. CLAP just gives us MIDI 1 (or 2 if you want, but I didn't code
         * that) streams to do with as you wish. The CLAP_MIDI_EVENT here does the obvious thing.
         */
        auto mevt = reinterpret_cast<const clap_event_midi *>(evt);
        auto msg = mevt->data[0] & 0xF0;
        auto chan = mevt->data[0] & 0x0F;
        switch (msg)
        {
        case 0x90:
        {
            // Hosts should prefer CLAP_NOTE events but if they don't
            handleNoteOn(mevt->port_index, chan, mevt->data[1], -1);
            break;
        }
        case 0x80:
        {
            // Hosts should prefer CLAP_NOTE events but if they don't
            handleNoteOff(mevt->port_index, chan, mevt->data[1]);
            break;
        }
        case 0xE0:
        {
            // pitch bend
            auto bv = (mevt->data[1] + mevt->data[2] * 128 - 8192) / 8192.0;

            for (auto &v : voices)
            {
                v.pitchBendWheel = bv * 2; // just hardcode a pitch bend depth of 2
                v.recalcPitch();
            }

            break;
        }
        }
        break;
    }
    /*
     * CLAP_EVENT_NOTE_ON and OFF simply deliver the event to the note creators below,
     * which find (probably) and activate a spare or playing voice. Our 'voice stealing'
     * algorithm here is 'just don't play a note 65 if 64 are ringing. Remember this is an
     * example synth!
     */
    case CLAP_EVENT_NOTE_ON:
    {
        auto nevt = reinterpret_cast<const clap_event_note *>(evt);
        handleNoteOn(nevt->port_index, nevt->channel, nevt->key, nevt->note_id);
    }
    break;
    case CLAP_EVENT_NOTE_OFF:
    {
        auto nevt = reinterpret_cast<const clap_event_note *>(evt);
        handleNoteOff(nevt->port_index, nevt->channel, nevt->key);
    }
    break;
    /*
     * CLAP_EVENT_PARAM_VALUE sets a value. What happens if you change a parameter
     * outside a modulation context. We simply update our engine value and, if an editor
     * is attached, send an editor message.
     */
    case CLAP_EVENT_PARAM_VALUE:
    {

        auto v = reinterpret_cast<const clap_event_param_value *>(evt);

        *paramToValue[v->param_id] = v->value;
        pushParamsToVoices();

        if (clapJuceShim->isEditorAttached())
        {
            auto r = ToUI();
            r.type = ToUI::PARAM_VALUE;
            r.id = v->param_id;
            r.value = (double)v->value;

            uiComms.toUiQ.try_enqueue(r);
        }
    }
    break;
    /*
     * CLAP_EVENT_PARAM_MOD provides both monophonic and polyphonic modulation.
     * We do this by seeing which parameter is modulated then adjusting the
     * side-by-side modulation values in a voice.
     */
    case CLAP_EVENT_PARAM_MOD:
    {
        auto pevt = reinterpret_cast<const clap_event_param_mod *>(evt);

        // This little lambda updates a modulation slot in a voice properly
        auto applyToVoice = [&pevt](auto &v) {
            if (!v.isPlaying())
                return;

            auto pd = pevt->param_id;
            switch (pd)
            {
            case paramIds::pmCutoff:
            {
                v.cutoffMod = pevt->amount;
                v.recalcFilter();
                break;
            }
            case paramIds::pmUnisonSpread:
            {
                v.uniSpreadMod = pevt->amount;
                v.recalcPitch();
                break;
            }
            case paramIds::pmOscDetune:
            {
                // CNDOUT << "Detune Mod" << CNDVAR(pevt->amount) << std::endl;
                v.oscDetuneMod = pevt->amount;
                v.recalcPitch();
                break;
            }
            case paramIds::pmResonance:
            {
                v.resMod = pevt->amount;
                v.recalcFilter();
                break;
            }
            case paramIds::pmPreFilterVCA:
            {
                v.preFilterVCAMod = pevt->amount;
            }
            }
        };

        /*
         * The real meat is here. If we have a note id, find the note and modulate it.
         * Otherwise if we have a key (we are doing "PCK modulation" rather than "noteid
         * modulation") find a voice and update that. Otherwise it is a monophonic modulation
         * so update every voice.
         */
        if (pevt->note_id >= 0)
        {
            // poly by note_id
            for (auto &v : voices)
            {
                if (v.note_id == pevt->note_id)
                {
                    applyToVoice(v);
                }
            }
        }
        else if (pevt->key >= 0 && pevt->channel >= 0 && pevt->port_index >= 0)
        {
            // poly by PCK
            for (auto &v : voices)
            {
                if (v.key == pevt->key && v.channel == pevt->channel &&
                    v.portid == pevt->port_index)
                {
                    applyToVoice(v);
                }
            }
        }
        else
        {
            // mono
            for (auto &v : voices)
            {
                applyToVoice(v);
            }
        }
    }
    break;
    /*
     * Note expression handling is similar to polymod. Traverse the voices - in note expression
     * indexed by channel / key / port - and adjust the modulation slot in each.
     */
    case CLAP_EVENT_NOTE_EXPRESSION:
    {
        auto pevt = reinterpret_cast<const clap_event_note_expression *>(evt);
        for (auto &v : voices)
        {
            if (!v.isPlaying())
                continue;

            // Note expressions work on key not note id
            if (v.key == pevt->key && v.channel == pevt->channel && v.portid == pevt->port_index)
            {
                switch (pevt->expression_id)
                {
                case CLAP_NOTE_EXPRESSION_VOLUME:
                    // I can mod the VCA
                    v.volumeNoteExpressionValue = pevt->value - 1.0;
                    break;
                case CLAP_NOTE_EXPRESSION_TUNING:
                    v.pitchNoteExpressionValue = pevt->value;
                    v.recalcPitch();
                    break;
                }
            }
        }
    }
    break;
    }
}
#if 0
void ConduitPolysynth::handleEventsFromUIQueue(const clap_output_events_t *ov)
{
    bool uiAdjustedValues{false};
    ConduitPolysynth::FromUI r;
    while (uiComms.fromUiQ.try_dequeue(r))
    {
        switch (r.type)
        {
        case FromUI::BEGIN_EDIT:
        case FromUI::END_EDIT:
        {
            auto evt = clap_event_param_gesture();
            evt.header.size = sizeof(clap_event_param_gesture);
            evt.header.type = (r.type == FromUI::BEGIN_EDIT ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                            : CLAP_EVENT_PARAM_GESTURE_END);
            evt.header.time = 0;
            evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            evt.header.flags = 0;
            evt.param_id = r.id;
            ov->try_push(ov, &evt.header);

            break;
        }
        case FromUI::ADJUST_VALUE:
        {
            // So set my value
            *paramToValue[r.id] = r.value;

            // But we also need to generate outbound message to the host
            auto evt = clap_event_param_value();
            evt.header.size = sizeof(clap_event_param_value);
            evt.header.type = (uint16_t)CLAP_EVENT_PARAM_VALUE;
            evt.header.time = 0; // for now
            evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            evt.header.flags = 0;
            evt.param_id = r.id;
            evt.value = r.value;

            ov->try_push(ov, &(evt.header));

            uiAdjustedValues = true;
        }
        }
    }

    // Similarly we need to push values to a UI on startup
    if (uiComms.refreshUIValues && clapJuceShim->isEditorAttached())
    {
        uiComms.refreshUIValues = false;

        for (const auto &[k, v] : paramToValue)
        {
            auto r = ToUI();
            r.type = ToUI::PARAM_VALUE;
            r.id = k;
            r.value = *v;
            uiComms.toUiQ.try_enqueue(r);
        }
    }

    if (uiAdjustedValues)
        pushParamsToVoices();
}
#endif

/*
 * The note on, note off, and push params to voices implementations are, basically, completely
 * uninteresting.
 */
void ConduitPolysynth::handleNoteOn(int port_index, int channel, int key, int noteid)
{
    bool foundVoice{false};
    for (auto &v : voices)
    {
        if (v.state == SawDemoVoice::OFF)
        {
            activateVoice(v, port_index, channel, key, noteid);
            foundVoice = true;
            break;
        }
    }

    if (!foundVoice)
    {
        // We could steal oldest. If you want to do that toss in a PR to add age
        // to the voice I guess. This is just a demo synth though.
        auto idx = rand() % max_voices;
        auto &v = voices[idx];
        terminatedVoices.emplace_back(v.portid, v.channel, v.key, v.note_id);
        activateVoice(v, port_index, channel, key, noteid);
    }

    uiComms.dataCopyForUI.updateCount++;
    uiComms.dataCopyForUI.polyphony++;

    if (clapJuceShim->isEditorAttached())
    {
        auto r = ToUI();
        r.type = ToUI::MIDI_NOTE_ON;
        r.id = (uint32_t)key;
        uiComms.toUiQ.try_enqueue(r);
    }
}

void ConduitPolysynth::handleNoteOff(int port_index, int channel, int n)
{
    for (auto &v : voices)
    {
        if (v.isPlaying() && v.key == n && v.portid == port_index && v.channel == channel)
        {
            v.release();
        }
    }

    if (clapJuceShim->isEditorAttached())
    {
        auto r = ToUI();
        r.type = ToUI::MIDI_NOTE_OFF;
        r.id = (uint32_t)n;
        uiComms.toUiQ.try_enqueue(r);
    }
}

void ConduitPolysynth::activateVoice(SawDemoVoice &v, int port_index, int channel, int key,
                                     int noteid)
{
    v.unison = std::max(1, std::min(7, (int)*unisonCount));
    v.filterMode = (int)static_cast<int>(*filterMode);
    v.note_id = noteid;
    v.portid = port_index;
    v.channel = channel;

    v.uniSpread = *unisonSpread;
    v.oscDetune = *oscDetune;
    v.cutoff = *cutoff;
    v.res = *resonance;
    v.preFilterVCA = *preFilterVCA;
    v.ampRelease = scaleTimeParamToSeconds(*ampRelease);
    v.ampAttack = scaleTimeParamToSeconds(*ampAttack);
    v.ampGate = *ampIsGate > 0.5;

    // reset all the modulations
    v.cutoffMod = 0;
    v.oscDetuneMod = 0;
    v.resMod = 0;
    v.preFilterVCAMod = 0;
    v.uniSpreadMod = 0;
    v.volumeNoteExpressionValue = 0;
    v.pitchNoteExpressionValue = 0;

    v.start(key);
}

/*
 * If the processing loop isn't running, the call to requestParamFlush from the UI will
 * result in this being called on the main thread, and generating all the appropriate
 * param updates.
 */
void ConduitPolysynth::paramsFlush(const clap_input_events *in,
                                   const clap_output_events *out) noexcept
{
    auto sz = in->size(in);

    // This pointer is the sentinel to our next event which we advance once an event is processed
    for (auto e = 0U; e < sz; ++e)
    {
        auto nextEvent = in->get(in, e);
        handleInboundEvent(nextEvent);
    }

    auto ct = handleEventsFromUIQueue(out);

    if (ct)
        pushParamsToVoices();

    // We will never generate a note end event with processing active, and we have no midi
    // output, so we are done.
}

void ConduitPolysynth::pushParamsToVoices()
{
    for (auto &v : voices)
    {
        if (v.isPlaying())
        {
            v.uniSpread = *unisonSpread;
            v.oscDetune = *oscDetune;
            v.cutoff = *cutoff;
            v.res = *resonance;
            v.preFilterVCA = *preFilterVCA;
            v.ampRelease = scaleTimeParamToSeconds(*ampRelease);
            v.ampAttack = scaleTimeParamToSeconds(*ampAttack);
            v.ampGate = *ampIsGate > 0.5;
            v.filterMode = *filterMode;

            v.recalcPitch();
            v.recalcFilter();
        }
    }
}

float ConduitPolysynth::scaleTimeParamToSeconds(float param)
{
    auto scaleTime = std::clamp((param - 2.0 / 3.0) * 6, -100.0, 2.0);
    auto res = powf(2.f, scaleTime);
    return res;
}

} // namespace sst::conduit::polysynth

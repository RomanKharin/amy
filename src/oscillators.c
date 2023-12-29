#include "amy.h"


// For checking assumptions about bitwidths.
#include <assert.h>

#include "sine_lutset_fxpt.h"
#include "impulse_lutset_fxpt.h"
#include "triangle_lutset_fxpt.h"

// For hardware random on ESP
#ifdef ESP_PLATFORM
#include <esp_system.h>
#endif


/* Dan Ellis libblosca functions */
const LUT *choose_from_lutset(float period, const LUT *lutset) {
    // Select the best entry from a lutset for a given period. 
    //
    // Args:
    //    period: (float) Target period of waveform in fractional samples.
    //    lutset: Sorted list of LUTs, as generated by create_lutset().
    //
    // Returns:
    //   One of the LUTs from the lutset, best suited to interpolating to generate
    //   a waveform of the desired period.
    // Use the earliest (i.e., longest, most harmonics) LUT that works
    // (i.e., will not actually cause aliasing).
    // So start with the highest-bandwidth (and longest) LUTs, but skip them
    // if they result in aliasing.
    const LUT *lut_table = NULL;
    int lut_size = 0;
    int lut_index = 0;
    while(lutset[lut_index].table_size > 0) {
        lut_table = &lutset[lut_index];
        lut_size = lutset[lut_index].table_size;
        // What proportion of nyquist does the highest harmonic in this table occupy?
        float lut_bandwidth = 2 * lutset[lut_index].highest_harmonic / (float)lut_size;
        // To complete one cycle of <lut_size> points in <period> steps, each step
        // will need to be this many samples:
        float lut_hop = lut_size / period;
        // If we have a signal with a given bandwidth, but then speed it up by 
        // skipping lut_hop samples per sample, its bandwidth will increase 
        // proportionately.
        float interp_bandwidth = lut_bandwidth * lut_hop;
        //printf("period=%f freq=%f lut_size=%d interp_bandwidth=%f\n", period, ((float)AMY_SAMPLE_RATE)/period, lut_size, interp_bandwidth);
        if (interp_bandwidth < 0.9) {
            // No aliasing, even with a 10% buffer (i.e., 19.8 kHz).
            break;
        }
        ++lut_index;
    }
    // At this point, we either got to the end of the LUT table, or we found a
    // table we could interpolate without aliasing.

    return lut_table;
}

// Multiple versions of render_lut with different features to avoid branches in sample loop.

#define RENDER_LUT_PREAMBLE \
    int lut_mask = lut->table_size - 1; \
    int lut_bits = lut->log_2_table_size; \
    SAMPLE sample = 0; \
    SAMPLE current_amp = incoming_amp; \
    SAMPLE incremental_amp = SHIFTR(ending_amp - incoming_amp, BLOCK_SIZE_BITS);

#define MOD_PART_MOD  \
            total_phase += S2P(mod[i]);

// Feedback is taken before output scaling.
#define FEEDBACK_PART_FB  \
            past1 = past0;  \
            past0 = sample;  \
            total_phase += S2P(MUL4_SS(feedback_level, SHIFTR(past1 + past0, 1)));

#define RENDER_LUT_GUTS(MOD_PART, FEEDBACK_PART, INTERP_PART) \
            MOD_PART \
            FEEDBACK_PART \
            int16_t base_index = INT_OF_P(total_phase, lut_bits); \
            SAMPLE frac = S_FRAC_OF_P(total_phase, lut_bits); \
            SAMPLE b = L2S(lut->table[base_index]); \
            SAMPLE c = L2S(lut->table[(base_index + 1) & lut_mask]); \
            INTERP_PART

#define INTERP_LINEAR \
            sample = b + MUL0_SS(c - b, frac);

// Miller's optimization -
// https://github.com/pure-data/pure-data/blob/db777311d808bb3ba728b94ab067f8d333b7d0c2/src/d_array.c#L831C1-L833C76
// outlet_float(x->x_obj.ob_outlet, b + frac * (
//    cminusb - 0.1666667f * (1.-frac) * (
//        (d - a - 3.0f * cminusb) * frac + (d + 2.0f*a - 3.0f*b))));#
#define INTERP_CUBIC \
            SAMPLE a = L2S(lut->table[(base_index - 1) & lut_mask]); \
            SAMPLE d = L2S(lut->table[(base_index + 2) & lut_mask]); \
            SAMPLE cminusb = c - b; \
            SAMPLE fr_d_ma_m3cmb = MUL0_SS(d - a - cminusb - SHIFTL(cminusb, 1), frac); \
            SAMPLE next_bit = MUL0_SS(fr_d_ma_m3cmb + d + SHIFTL(a - b, 1) - b, MUL0_SS(F2S(1.0f) - frac, F2S(0.16666666666667f))); \
            sample = b + MUL0_SS(cminusb - next_bit, frac);

#define RENDER_LUT_LOOP_END \
            buf[i] += MUL4_SS(current_amp, sample); \
            current_amp += incremental_amp; \
            phase = P_WRAPPED_SUM(phase, step);


#define NOTHING ;


PHASOR render_lut_fm_fb(SAMPLE* buf,
                        PHASOR phase, 
                        PHASOR step,
                        SAMPLE incoming_amp, SAMPLE ending_amp,
                        const LUT* lut,
                        SAMPLE* mod, SAMPLE feedback_level, SAMPLE* last_two) { 
    RENDER_LUT_PREAMBLE
    SAMPLE past0 = 0, past1 = 0;
    sample = last_two[0];
    past0 = last_two[1];
    for(uint16_t i = 0; i < AMY_BLOCK_SIZE; i++) {
        PHASOR total_phase = phase;

        RENDER_LUT_GUTS(MOD_PART_MOD, FEEDBACK_PART_FB, INTERP_LINEAR)

        RENDER_LUT_LOOP_END
    }
    last_two[0] = sample;
    last_two[1] = past0;
    return phase;
}

PHASOR render_lut_fb(SAMPLE* buf,
                     PHASOR phase,
                     PHASOR step,
                     SAMPLE incoming_amp, SAMPLE ending_amp,
                     const LUT* lut,
                     SAMPLE feedback_level, SAMPLE* last_two) {
    RENDER_LUT_PREAMBLE
    SAMPLE past0 = 0, past1 = 0;
    sample = last_two[0];
    past0 = last_two[1];
    for(uint16_t i = 0; i < AMY_BLOCK_SIZE; i++) {
        PHASOR total_phase = phase;

        RENDER_LUT_GUTS(NOTHING, FEEDBACK_PART_FB, INTERP_LINEAR)

        RENDER_LUT_LOOP_END
    }
    last_two[0] = sample;
    last_two[1] = past0;
    return phase;
}

PHASOR render_lut_fm(SAMPLE* buf,
                     PHASOR phase,
                     PHASOR step,
                     SAMPLE incoming_amp, SAMPLE ending_amp,
                     const LUT* lut,
                     SAMPLE* mod) {
    RENDER_LUT_PREAMBLE
    for(uint16_t i = 0; i < AMY_BLOCK_SIZE; i++) {
        PHASOR total_phase = phase;

        RENDER_LUT_GUTS(MOD_PART_MOD, NOTHING, INTERP_LINEAR)

        RENDER_LUT_LOOP_END
    }
    return phase;
}

PHASOR render_lut(SAMPLE* buf,
                  PHASOR phase,
                  PHASOR step,
                  SAMPLE incoming_amp, SAMPLE ending_amp,
                  const LUT* lut) {
    RENDER_LUT_PREAMBLE
    for(uint16_t i = 0; i < AMY_BLOCK_SIZE; i++) {
        PHASOR total_phase = phase;

        RENDER_LUT_GUTS(NOTHING, NOTHING, INTERP_LINEAR)

        RENDER_LUT_LOOP_END
     }
    return phase;
}

PHASOR render_lut_cub(SAMPLE* buf,
                      PHASOR phase,
                      PHASOR step,
                      SAMPLE incoming_amp, SAMPLE ending_amp,
                      const LUT* lut) {
    RENDER_LUT_PREAMBLE
    for(uint16_t i = 0; i < AMY_BLOCK_SIZE; i++) {
        PHASOR total_phase = phase;

        RENDER_LUT_GUTS(NOTHING, NOTHING, INTERP_CUBIC)

        RENDER_LUT_LOOP_END
     }
    return phase;
}

void lpf_buf(SAMPLE *buf, SAMPLE decay, SAMPLE *state) {
    // Implement first-order low-pass (leaky integrator).
    SAMPLE s = *state;
    for (uint16_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        buf[i] += MUL4_SS(decay, s);
        s = buf[i];
    }
    *state = s;
}


/* Pulse wave */
void pulse_note_on(uint16_t osc, float freq) {
    //printf("pulse_note_on: time %lld osc %d logfreq %f amp %f last_amp %f\n", total_samples, osc, synth[osc].logfreq, S2F(synth[osc].amp), S2F(synth[osc].last_amp));
    float period_samples = (float)AMY_SAMPLE_RATE / freq;
    synth[osc].lut = choose_from_lutset(period_samples, impulse_fxpt_lutset);
    // Tune the initial integrator state to compensate for mid-sample alignment of table.
    float float_amp = synth[osc].amp_coefs[0] * freq * 4.0f / AMY_SAMPLE_RATE;
    synth[osc].lpf_state = MUL4_SS(F2S(-0.5 * float_amp), L2S(synth[osc].lut->table[0]));
}

void render_lpf_lut(SAMPLE* buf, uint16_t osc, int8_t is_square, int8_t direction, SAMPLE dc_offset) {
    // Common function for pulse and saw.
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);  // cycles per sec / samples per sec -> cycles per sample
    // LPF time constant should be ~ 10x osc period, so droop is minimal.
    // alpha = 1 - 1 / t_const; t_const = 10 / m_freq, so alpha = 1 - m_freq / 10
    synth[osc].lpf_alpha = F2S(1.0f - freq / (10.0f * AMY_SAMPLE_RATE));
    // Scale the impulse proportional to the phase increment step so its integral remains ~constant.
    const LUT *lut = synth[osc].lut;
    SAMPLE amp = direction * F2S(msynth[osc].amp * P2F(step) * 4.0f * lut->scale_factor);
    PHASOR pwm_phase = synth[osc].phase;
    synth[osc].phase = render_lut_cub(buf, synth[osc].phase, step, synth[osc].last_amp, amp, lut);
    if (is_square) {  // For pulse only, add a second delayed negative LUT wave.
        float duty = msynth[osc].duty;
        if (duty < 0.01f) duty = 0.01f;
        if (duty > 0.99f) duty = 0.99f;
        pwm_phase = P_WRAPPED_SUM(pwm_phase, F2P(msynth[osc].last_duty));
        // Second pulse is given some blockwise-constant FM to maintain phase continuity across blocks.
        PHASOR delta_phase_per_sample = F2P((duty - msynth[osc].last_duty) / AMY_BLOCK_SIZE);
        render_lut_cub(buf, pwm_phase, step + delta_phase_per_sample, -synth[osc].last_amp, -amp, synth[osc].lut);
        msynth[osc].last_duty = duty;
    }
    if (dc_offset) {
        // For saw only, apply a dc shift so integral is ~0.
        // But we have to apply the linear amplitude env on top as well, copying the way it's done in render_lut.
        SAMPLE current_amp = synth[osc].last_amp;
        SAMPLE incremental_amp = SHIFTR(amp - synth[osc].last_amp, BLOCK_SIZE_BITS); // i.e. delta(amp) / BLOCK_SIZE
        for (int i = 0; i < AMY_BLOCK_SIZE; ++i) {
            buf[i] += MUL4_SS(current_amp, dc_offset);
            current_amp += incremental_amp;
        }
    }        
    // LPF to integrate to convert pair of (+, -) impulses into a rectangular wave.
    SAMPLE alpha = synth[osc].lpf_alpha;
    if (msynth[osc].amp == 0 && synth[osc].last_amp == 0) {
        // When amp is zero, decay LPF more rapidly.
        alpha = F2S(1.0f) - SHIFTL(F2S(1.0f) - alpha, 4);
    }
    lpf_buf(buf, alpha, &synth[osc].lpf_state);
    // Remember last_amp.
    synth[osc].last_amp = amp;
}

void render_pulse(SAMPLE* buf, uint16_t osc) {
    // Second (negative) impulse is <duty> cycles later.
    render_lpf_lut(buf, osc, true, 1, 0);
}

void pulse_mod_trigger(uint16_t osc) {
    //float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    //float freq = freq_of_logfreq(synth[osc].logfreq);
    //float period = 1. / (freq/mod_sr);
    //synth[osc].step = period * synth[osc].phase;
}

// dpwe sez to use this method for low-freq mod pulse still 
SAMPLE compute_mod_pulse(uint16_t osc) {
    // do BW pulse gen at SR=44100/64
    if(msynth[osc].duty < 0.001f || msynth[osc].duty > 0.999) msynth[osc].duty = 0.5;
    if(synth[osc].phase >= F2P(msynth[osc].duty)) {
        synth[osc].sample = F2S(1.0f);
    } else {
        synth[osc].sample = F2S(-1.0f);
    }
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;  // samples per sec / samples per call = calls per sec
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, F2P(freq / mod_sr));  // cycles per sec / calls per sec = cycles per call
    return MUL4_SS(synth[osc].sample, F2S(msynth[osc].amp));
}


/* Saw waves */
void saw_note_on(uint16_t osc, int8_t direction_notused, float freq) {
    //printf("saw_note_on: time %lld osc %d freq %f logfreq %f amp %f last_amp %f phase %f\n", total_samples, osc, freq, synth[osc].logfreq, synth[osc].amp, S2F(synth[osc].last_amp), P2F(synth[osc].phase));
    float period_samples = ((float)AMY_SAMPLE_RATE / freq);
    synth[osc].lut = choose_from_lutset(period_samples, impulse_fxpt_lutset);
    // Calculate the mean of the LUT.
    SAMPLE lut_sum = 0;
    for (int i = 0; i < synth[osc].lut->table_size; ++i) {
        lut_sum += L2S(synth[osc].lut->table[i]);
    }
    int lut_bits = synth[osc].lut->log_2_table_size;
    synth[osc].dc_offset = -SHIFTR(lut_sum, lut_bits);
    synth[osc].lpf_state = 0;
    synth[osc].last_amp = 0;
}

void saw_down_note_on(uint16_t osc, float freq) {
    saw_note_on(osc, -1, freq);
}
void saw_up_note_on(uint16_t osc, float freq) {
    saw_note_on(osc, 1, freq);
}

void render_saw(SAMPLE* buf, uint16_t osc, int8_t direction) {
    render_lpf_lut(buf, osc, false, direction, synth[osc].dc_offset);
    //printf("render_saw: time %lld osc %d buf[]=%f %f %f %f %f %f %f %f\n",
    //       total_samples, osc, S2F(buf[0]), S2F(buf[1]), S2F(buf[2]), S2F(buf[3]), S2F(buf[4]), S2F(buf[5]), S2F(buf[6]), S2F(buf[7]));
}

void render_saw_down(SAMPLE* buf, uint16_t osc) {
    render_saw(buf, osc, -1);
}
void render_saw_up(SAMPLE* buf, uint16_t osc) {
    render_saw(buf, osc, 1);
}


void saw_mod_trigger(uint16_t osc) {
    //float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    //float freq = freq_of_logfreq(synth[osc].logfreq);
    //float period = 1. / (freq/mod_sr);
    //synth[osc].step = period * synth[osc].phase;
}

void saw_up_mod_trigger(uint16_t osc) {
    saw_mod_trigger(osc);
}
void saw_down_mod_trigger(uint16_t osc) {
    saw_mod_trigger(osc);
}

// TODO -- this should use dpwe code
SAMPLE compute_mod_saw(uint16_t osc, int8_t direction) {
    // Saw waveform is just the phasor.
    synth[osc].sample = SHIFTL(P2S(synth[osc].phase), 1) - F2S(1.0f);
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;  // samples per sec / samples per call = calls per sec
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, F2P(freq / mod_sr));  // cycles per sec / calls per sec = cycles per call
    return MUL4_SS(synth[osc].sample, direction * F2S(msynth[osc].amp));
}

SAMPLE compute_mod_saw_down(uint16_t osc) {
    return compute_mod_saw(osc, -1);
}

SAMPLE compute_mod_saw_up(uint16_t osc) {
    return compute_mod_saw(osc, 1);
}



/* triangle wave */
void triangle_note_on(uint16_t osc, float freq) {
    float period_samples = (float)AMY_SAMPLE_RATE / freq;
    synth[osc].lut = choose_from_lutset(period_samples, triangle_fxpt_lutset);
}

void render_triangle(SAMPLE* buf, uint16_t osc) {
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);  // cycles per sec / samples per sec -> cycles per sample
    SAMPLE amp = F2S(msynth[osc].amp);
    synth[osc].phase = render_lut(buf, synth[osc].phase, step, synth[osc].last_amp, amp, synth[osc].lut);
    synth[osc].last_amp = amp;
}

void triangle_mod_trigger(uint16_t osc) {
    // float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    // float freq = freq_of_logfreq(synth[osc].logfreq);
    // float period = 1. / (freq/mod_sr);
    // synth[osc].step = period * synth[osc].phase;
}

// TODO -- this should use dpwe code 
SAMPLE compute_mod_triangle(uint16_t osc) {
    // Saw waveform is just the phasor.
    SAMPLE sample = SHIFTL(P2S(synth[osc].phase), 2);  // 0..4
    if (sample > F2S(2.0f))  sample = F2S(4.0f) - sample;  // 0..2..0
    synth[osc].sample = sample - F2S(1.0f);  // -1 .. 1
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;  // samples per sec / samples per call = calls per sec
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, F2P(freq / mod_sr));  // cycles per sec / calls per sec = cycles per call
    return MUL4_SS(synth[osc].sample, F2S(msynth[osc].amp));
}

extern uint32_t total_samples;

/* FM */
// NB this uses new lingo for step, skip, phase etc
void fm_sine_note_on(uint16_t osc, uint16_t algo_osc) {
    if(AMY_IS_SET(synth[osc].logratio)) {
        msynth[osc].logfreq = msynth[algo_osc].logfreq + synth[osc].logratio;
    }
    // An empty exercise since there is only one entry in sine_lutset.
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    float period_samples = (float)AMY_SAMPLE_RATE / freq;
    synth[osc].lut = choose_from_lutset(period_samples, sine_fxpt_lutset);
}

void render_fm_sine(SAMPLE* buf, uint16_t osc, SAMPLE* mod, SAMPLE feedback_level, uint16_t algo_osc, SAMPLE mod_amp) {
    if(AMY_IS_SET(synth[osc].logratio)) {
        msynth[osc].logfreq = msynth[algo_osc].logfreq + synth[osc].logratio;
    }
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);  // cycles per sec / samples per sec -> cycles per sample
    SAMPLE amp = MUL4_SS(F2S(msynth[osc].amp), mod_amp);
    if (feedback_level && mod)
        synth[osc].phase = render_lut_fm_fb(buf, synth[osc].phase, step,
                                            synth[osc].last_amp, amp,
                                            synth[osc].lut,
                                            mod, feedback_level, synth[osc].last_two);
    else if (feedback_level)
        synth[osc].phase = render_lut_fb(buf, synth[osc].phase, step,
                                         synth[osc].last_amp, amp,
                                         synth[osc].lut,
                                         feedback_level, synth[osc].last_two);
    else if (mod)
        synth[osc].phase = render_lut_fm(buf, synth[osc].phase, step,
                                         synth[osc].last_amp, amp,
                                         synth[osc].lut,
                                         mod);
    else
        synth[osc].phase = render_lut(buf, synth[osc].phase, step,
                                      synth[osc].last_amp, amp,
                                      synth[osc].lut);

    synth[osc].last_amp = amp;
}

/* sine */
void sine_note_on(uint16_t osc, float freq) {
    //printf("sine_note_on: osc %d logfreq %f\n", osc, synth[osc].logfreq);
    // There's really only one sine table, but for symmetry with the other ones...
    float period_samples = (float)AMY_SAMPLE_RATE / freq;
    synth[osc].lut = choose_from_lutset(period_samples, sine_fxpt_lutset);
}

void render_sine(SAMPLE* buf, uint16_t osc) { 
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);  // cycles per sec / samples per sec -> cycles per sample
    SAMPLE amp = F2S(msynth[osc].amp);
    //printf("render_sine: osc %d freq %f amp %f\n", osc, P2F(step), S2F(amp));
    synth[osc].phase = render_lut(buf, synth[osc].phase, step, synth[osc].last_amp, amp, synth[osc].lut);
    synth[osc].last_amp = amp;
}


// TOOD -- not needed anymore
SAMPLE compute_mod_sine(uint16_t osc) { 
    // One sample pulled out of render_lut.
    const LUT *lut = synth[osc].lut;
    int lut_mask = lut->table_size - 1;
    int lut_bits = lut->log_2_table_size;
    int16_t base_index = INT_OF_P(synth[osc].phase, lut_bits);
    SAMPLE frac = S_FRAC_OF_P(synth[osc].phase, lut_bits);
    LUTSAMPLE b = lut->table[base_index];
    LUTSAMPLE c = lut->table[(base_index + 1) & lut_mask];
    synth[osc].sample = L2S(b) + MUL0_SS(L2S(c - b), frac);
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;  // samples per sec / samples per call = calls per sec
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, F2P(freq / mod_sr));  // cycles per sec / calls per sec = cycles per call
    return MUL4_SS(synth[osc].sample, F2S(msynth[osc].amp));
}

void sine_mod_trigger(uint16_t osc) {
    sine_note_on(osc, freq_of_logfreq(msynth[osc].logfreq));
}

// Returns a SAMPLE between -1 and 1.
SAMPLE amy_get_random() {
#ifndef AMY_USE_FIXEDPOINT
    return ((float)rand() / 2147483647.0) - 0.5;
#else
    assert(RAND_MAX == 2147483647); // 2^31 - 1
    return SHIFTR((SAMPLE)rand(), (31 - S_FRAC_BITS)) - F2S(0.5);
#endif
}

/* noise */

void render_noise(SAMPLE *buf, uint16_t osc) {
    SAMPLE amp = F2S(msynth[osc].amp);
    for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
        buf[i] = MUL4_SS(amy_get_random(), amp);
    }
}

SAMPLE compute_mod_noise(uint16_t osc) {
    float mod_sr = (float)AMY_SAMPLE_RATE / (float)AMY_BLOCK_SIZE;
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    float fstep = freq / mod_sr;
    SAMPLE amp = F2S(msynth[osc].amp);
    PHASOR starting_phase = synth[osc].phase;
    synth[osc].phase = P_WRAPPED_SUM(synth[osc].phase, F2P(fstep));  // cycles per sec / calls per sec = cycles per call
    if (fstep > 1.0f || synth[osc].phase < starting_phase) {
        // phase wrapped, take new sample.
        synth[osc].last_two[0] = MUL4_SS(amy_get_random(), amp);
    }
    //printf("mod_noise: time %lld fstep %f samp %f\n", total_samples, fstep, S2F(synth[osc].last_two[0]));
    return synth[osc].last_two[0];
}



/* partial */

#if AMY_HAS_PARTIALS == 1

void render_partial(SAMPLE * buf, uint16_t osc) {
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    PHASOR step = F2P(freq / (float)AMY_SAMPLE_RATE);  // cycles per sec / samples per sec -> cycles per sample
    SAMPLE amp = F2S(msynth[osc].amp);
    //printf("render_partial: time %lld logfreq %f freq %f amp %f step %f\n", total_samples, msynth[osc].logfreq, freq, S2F(amp), P2F(step) * synth[osc].lut->table_size); 
    synth[osc].phase = render_lut(buf, synth[osc].phase, step, synth[osc].last_amp, amp, synth[osc].lut);
    synth[osc].last_amp = amp;
}

void partial_note_on(uint16_t osc) {
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    float period_samples = (float)AMY_SAMPLE_RATE / freq;
    synth[osc].lut = choose_from_lutset(period_samples, sine_fxpt_lutset);
}

void partial_note_off(uint16_t osc) {
    synth[osc].substep = 2;
    AMY_UNSET(synth[osc].note_on_clock);
    synth[osc].note_off_clock = total_samples;   
    synth[osc].last_amp = 0;
    synth[osc].status = OFF;
}

#endif

#if AMY_KS_OSCS > 0

#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
SAMPLE ** ks_buffer;
uint8_t ks_polyphony_index;


/* karplus-strong */

void render_ks(SAMPLE * buf, uint16_t osc) {
    SAMPLE half = MUL0_SS(F2S(0.5f), F2S(synth[osc].feedback));
    SAMPLE amp = F2S(msynth[osc].amp);
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    if(freq >= 55) { // lowest note we can play
        uint16_t buflen = (uint16_t)(AMY_SAMPLE_RATE / freq);
        for(uint16_t i=0;i<AMY_BLOCK_SIZE;i++) {
            uint16_t index = (uint16_t)(synth[osc].step);
            synth[osc].sample = ks_buffer[ks_polyphony_index][index];
            ks_buffer[ks_polyphony_index][index] =                 
                MUL4_SS(
                    (ks_buffer[ks_polyphony_index][index] + ks_buffer[ks_polyphony_index][(index + 1) % buflen]),
                    half);
            synth[osc].step = (index + 1) % buflen;
            buf[i] = MUL4_SS(synth[osc].sample, amp);
        }
    }
}

void ks_note_on(uint16_t osc) {
    float freq = freq_of_logfreq(msynth[osc].logfreq);
    if(freq <= 1.f) freq = 1.f;
    uint16_t buflen = (uint16_t)(AMY_SAMPLE_RATE / freq);
    if(buflen > MAX_KS_BUFFER_LEN) buflen = MAX_KS_BUFFER_LEN;
    // init KS buffer with noise up to max
    for(uint16_t i=0;i<buflen;i++) {
        ks_buffer[ks_polyphony_index][i] = amy_get_random();
    }
    ks_polyphony_index++;
    if(ks_polyphony_index == AMY_KS_OSCS) ks_polyphony_index = 0;
}

void ks_note_off(uint16_t osc) {
    msynth[osc].amp = 0;
}


void ks_init(void) {
    // 6ms buffer
    ks_polyphony_index = 0;
    ks_buffer = (SAMPLE**) malloc(sizeof(SAMPLE*)*AMY_KS_OSCS);
    for(int i=0;i<AMY_KS_OSCS;i++) ks_buffer[i] = (SAMPLE*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

void ks_deinit(void) {
    for(int i=0;i<AMY_KS_OSCS;i++) free(ks_buffer[i]);
    free(ks_buffer);
}
#endif

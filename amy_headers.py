# amy_headers.py
# Generate headers for libAMY
import sys

import numpy as np

def generate_amy_pcm_header(sample_set, name, pcm_AMY_SAMPLE_RATE=22050):
    from sf2utils.sf2parse import Sf2File
    import resampy
    import struct
    # These are the indexes that we liked and fit into the flash on ESP32. You can download the sf2 files here:
    # https://github.com/vigliensoni/soundfonts/blob/master/hs_tr808/HS-TR-808-Drums.sf2
    # https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf2
    fns = ( ("sounds/HS-TR-808-Drums.sf2", False), ('sounds/MuseScore_General.sf2', True))
    offsets = []
    offset = 0
    int16s = []
    samples = []
    sample_counter = 0
    my_sample_counter = 0
    orig_map = {}
    weak = ""
    if(name=='large'):
        weak = "__attribute__((weak)) "
    for (fn, is_inst) in fns:
        try:
            sf2 = Sf2File(open(fn, 'rb'))
        except:
            print("For PCM patches, download the sf2 files first. See comment in amy_headers.generate_amy_pcm_header()")
            return
        if is_inst:
            for i,inst in enumerate(sf2.instruments[:-1]):
                b = inst.bags[int(len(inst.bags)/2)]
                if(sample_counter in sample_set):
                    samples.append(b.sample)
                    orig_map[my_sample_counter] = sample_counter
                    my_sample_counter += 1
                sample_counter += 1
        else:
            for sample in sf2.samples[:-1]:
                if(sample_counter in sample_set):
                    samples.append(sample)
                    orig_map[my_sample_counter] = sample_counter
                    my_sample_counter += 1
                sample_counter += 1
    for sample in samples:
        try:
            s = {}
            s["name"] = sample.name
            floaty =(np.frombuffer(bytes(sample.raw_sample_data),dtype='int16'))/32768.0
            resampled = resampy.resample(floaty, sample.sample_rate, pcm_AMY_SAMPLE_RATE)
            # Make sure the float value doesn't cause overflow in int.  resampling can cause overshoot.
            samples_int16 = np.int16(np.minimum(32767.0, np.maximum(-32768.0, resampled*32768.0)))
            #floats.append(resampled)
            int16s.append(samples_int16)
            s["offset"] = offset 
            s["length"] = resampled.shape[0]
            s["loopstart"] = int(float(sample.start_loop) / float(sample.sample_rate / pcm_AMY_SAMPLE_RATE))
            s["loopend"] = int(float(sample.end_loop) / float(sample.sample_rate / pcm_AMY_SAMPLE_RATE))
            s["midinote"] = sample.original_pitch
            offset = offset + resampled.shape[0]
            offsets.append(s)
        except AttributeError:
            print("skipping %s" % (sample.name))
    
    all_samples = np.hstack(int16s)
    # Write packed .bin file of pcm[] as well as .h file to write as an ESP32 binary partition
    b = open("src/pcm_%s.bin" % (name), "wb")
    for i in range(all_samples.shape[0]):
        b.write(struct.pack('<h', all_samples[i]))
    b.close()
    p = open("src/pcm_%s.h" % (name), "w")
    p.write("// Automatically generated by amy_headers.generate_pcm_header()\n")
    p.write("#ifndef __PCM_H\n#define __PCM_H\n#include \"pcm_samples_%s.h\"\n" % (name))
    p.write("%sconst uint16_t pcm_samples=%d;\n#define PCM_LENGTH %d\n#define PCM_AMY_SAMPLE_RATE %d\n" % (weak, len(offsets), all_samples.shape[0], pcm_AMY_SAMPLE_RATE))
    p.write("%sconst pcm_map_t pcm_map[%d] PROGMEM = {\n" % (weak, len(offsets)))
    for i,o in enumerate(offsets):
        p.write("    /* [%d] %d */ {%d, %d, %d, %d, %d}, /* %s */\n" %(i, orig_map[i], o["offset"], o["length"], o["loopstart"], o["loopend"], o["midinote"], o["name"]))
    p.write("};\n")
    p.write("\n#endif  // __PCM_H\n")
    p.close()

    p = open("src/pcm_samples_%s.h" % (name), 'w')
    p.write("// Automatically generated by amy_headers.generate_pcm_header()\n")
    p.write("#ifndef __PCM_SAMPLES_H\n#define __PCM_SAMPLES_H\n")
    p.write("%sconst int16_t pcm[%d] PROGMEM = {\n" % (weak, all_samples.shape[0]))

    column = 15
    count = 0
    for i in range(int(all_samples.shape[0]/column)):
        p.write("    %s,\n" % (",".join([("%d" % (d)).ljust(8) for d in all_samples[i*column:(i+1)*column]])))
        count = count + column
    print("count %d all_samples.shape %d" % (count, all_samples.shape[0]))
    if(count != all_samples.shape[0]):
        p.write("    %s\n" % (",".join([("%d" % (d)).ljust(8) for d in all_samples[count:]])))
    p.write("};\n")
    p.write("\n#endif  // __PCM_SAMPLES_H\n")


def generate_both_pcm_headers():
    large = [0, 3, 8, 11, 14, 16, 17, 18, 20, 23, 25, 26, 29, 30, 31, 32, 37, 39, 40, 42, 47, 49, 50, 52, 58, 63, 69, 74, 76, 80, 83, \
        85, 86, 95, 96, 99, 100, 101, 107, 108, 109, 112, 116, 117, 118, 120, 127, \
        130, 134, 136, 145, 149, 155, 161, 165, 166, 170, 171, 175, 177, 178, 183, 192, 197, 198, 200, 204]
    # The small set is for flash constrained devices (Tulip CC)
    small = [0, 3, 8, 11, 14, 16, 17, 18, 20, 23, 25, 26, 29, 30, 31, 32, 37, 39, 58, 83, 85, 86, 116, 117, 118, 120, 127, 130, 136]
    # Tiny set for even more constrained devices 
    tiny = small[0:11]
    generate_amy_pcm_header(large, "large")
    generate_amy_pcm_header(small, "small")
    generate_amy_pcm_header(tiny, "tiny")


def cos_lut(table_size, harmonics_weights, harmonics_phases=None):
    if harmonics_phases is None:
        harmonics_phases = np.zeros(len(harmonics_weights))
    table = np.zeros(table_size)
    phases = np.arange(table_size) * 2 * np.pi / table_size
    for harmonic_number, harmonic_weight in enumerate(harmonics_weights):
        table += harmonic_weight * np.cos(
            phases * harmonic_number + harmonics_phases[harmonic_number])
    return table


# A LUTset is a list of LUTentries describing downsampled versions of the same
# basic waveform, sorted with the longest (highest-bandwidth) first.
def create_lutset(LUTentry, harmonic_weights, harmonic_phases=None, 
                  length_factor=8, bandwidth_factor=None):
    if bandwidth_factor is None:
        bandwidth_factor = np.sqrt(0.5)
    """Create an ordered list of LUTs with decreasing harmonic content.

    These can then be used in interp_from_lutset to make an adaptive-bandwidth
    interpolation.

    Args:
        harmonic_weights: vector of amplitudes for cosine harmonic components.
        harmonic_phases: initial phases for each harmonic, in radians. Zero 
            (default) indicates cosine phase.
        length_factor: Each table's length is at least this factor times the order
            of the highest harmonic it contains. Thus, this is a lower bound on the
            number of samples per cycle for the highest harmonic. Higher factors make
            the interpolation easier.
        bandwidth_factor: Target ratio between the highest harmonics in successive
            table entries. Default is sqrt(0.5), so after two tables, bandwidth is
            reduced by 1/2 (and length with follow).

    Returns:
        A list of LUTentry objects, sorted in decreasing order of the highest 
        harmonic they contain. Each LUT's length is a power of 2, and as small as
        possible while respecting the length_factor for the highest contained 
        harmonic.
    """
    if harmonic_phases is None:
        harmonic_phases = np.zeros(len(harmonic_weights))
    # Calculate the length of the longest LUT we need. Must be a power of 2, 
    # must have at least length_factor * highest_harmonic samples.
    # Harmonic 0 (dc) doesn't count.
    float_num_harmonics = float(len(harmonic_weights))
    lutsets = []
    done = False
    # harmonic 0 is DC; there's no point in generating that table.
    while float_num_harmonics >= 2:
        num_harmonics = int(round(float_num_harmonics))
        highest_harmonic = num_harmonics - 1    # because zero doesn't count.
        lut_size = int(2 ** np.ceil(np.log(length_factor * highest_harmonic) / np.log(2)))
        lutsets.append(LUTentry(
                table=cos_lut(lut_size, harmonic_weights[:num_harmonics], 
                                harmonic_phases[:num_harmonics]),    # / lut_size,
                highest_harmonic=highest_harmonic))
        float_num_harmonics = bandwidth_factor * float_num_harmonics
    return lutsets


def make_table(min_val, max_val, fn, table_size=257, dtype=np.int16):
    # The table includes the final value, so the actual number of steps is table_size - 1.
    steps = table_size - 1
    stepsize = (max_val - min_val) / steps
    return fn(np.arange(min_val, max_val + stepsize, stepsize)).astype(dtype)


def create_exp2_lut(npts):
    exp2_int16_fn = lambda x: np.round(-32768.0 * (np.exp2(x) - 1.0))
    return make_table(0, 1, exp2_int16_fn, table_size=npts, dtype=np.int16)


def create_log2_lut(npts):
    log2_int16_fn = lambda x: np.round(-32768.0 * (np.log2(x + 1.0)))
    return make_table(0, 1, log2_int16_fn, table_size=npts, dtype=np.int16)


def write_lutset_to_h(filename, variable_base, lutset):
    """Savi out a lutset as a C-compatible header file."""
    num_luts = len(lutset)
    with open(filename, "w") as f:
        f.write("// Automatically-generated LUTset\n")
        f.write("#ifndef LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
        f.write("#define LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
        f.write("\n")
        # Define the structure.
        f.write("#ifndef LUTENTRY_DEFINED\n")
        f.write("#define LUTENTRY_DEFINED\n")
        f.write("typedef struct {\n")
        f.write("    const float *table;\n")
        f.write("    int table_size;\n")
        f.write("    int highest_harmonic;\n")
        f.write("} lut_entry;\n")
        f.write("#endif // LUTENTRY_DEFINED\n")
        f.write("\n")
        # Define the content of the individual tables.
        samples_per_row = 8
        for i in range(num_luts):
            table_size = len(lutset[i].table)
            f.write("const float {:s}_lutable_{:d}[{:d}] PROGMEM = {{\n".format(
                variable_base, i, table_size))
            for row_start in range(0, table_size, samples_per_row):
                for sample_index in range(row_start,
                        min(row_start + samples_per_row, table_size)):
                    f.write("{:f},".format(lutset[i].table[sample_index]))
                f.write("\n")
            f.write("};\n")
            f.write("\n")
        # Define the table of LUTs.
        f.write("lut_entry {:s}_lutset[{:d}] = {{\n".format(
            variable_base, num_luts + 1))
        for i in range(num_luts):
            f.write("    {{{:s}_lutable_{:d}, {:d}, {:d}}},\n".format(
                variable_base, i, len(lutset[i].table), 
                lutset[i].highest_harmonic))
        # Final entry is null to indicate end of table.
        f.write("    {NULL, 0, 0},\n")
        f.write("};\n")
        f.write("\n")
        f.write("#endif // LUTSET_x_DEFINED\n")
    print("wrote", filename)


def write_fxpt_lutable(f, lutable, name, samples_per_row=8):
    """Write a single lutable to an open file."""
    table_size = len(lutable)
    scale_factor = np.max(np.abs(lutable.astype(float)))
    f.write("const int16_t {:s}[{:d}] PROGMEM = {{\n".format(
        name, table_size))
    for row_start in range(0, table_size, samples_per_row):
        for sample_index in range(row_start,
                                  min(row_start + samples_per_row, table_size)):
            f.write("{:d},".format(
                min(32767,
                    max(-32768,
                        int(round(32768 / scale_factor * lutable[sample_index]))))))
        f.write("\n")
    f.write("};\n")
    f.write("\n")
    return scale_factor


def write_lutset_to_h_as_fxpt(filename, variable_base, lutset):
    """Save out a lutset as a C-compatible header file using ints."""
    import math
    num_luts = len(lutset)
    with open(filename, "w") as f:
        f.write("// Automatically-generated LUTset\n")
        f.write("#ifndef LUTSET_{:s}_FXPT_DEFINED\n".format(variable_base.upper()))
        f.write("#define LUTSET_{:s}_FXPT_DEFINED\n".format(variable_base.upper()))
        f.write("\n")
        # Define the structure.
        f.write("#ifndef LUTENTRY_FXPT_DEFINED\n")
        f.write("#define LUTENTRY_FXPT_DEFINED\n")
        f.write("typedef struct {\n")
        f.write("    const int16_t *table;\n")
        f.write("    int table_size;\n")
        f.write("    int log_2_table_size;\n")
        f.write("    int highest_harmonic;\n")
        f.write("    float scale_factor;\n")
        f.write("} lut_entry_fxpt;\n")
        f.write("#endif // LUTENTRY_FXPT_DEFINED\n")
        f.write("\n")
        # Define the content of the individual tables.
        scale_factors = []
        for i in range(num_luts):
            scale_factor = write_fxpt_lutable(
                f, lutset[i].table,
                '{:s}_fxpt_lutable_{:d}'.format(variable_base, i)
            )
            scale_factors.append(scale_factor)
        # Define the table of LUTs.
        f.write("lut_entry_fxpt {:s}_fxpt_lutset[{:d}] = {{\n".format(
            variable_base, num_luts + 1))
        for i in range(num_luts):
            table_size = len(lutset[i].table)
            # Provide the shift size corresponding to the lutset.
            log_2_table_size = int(round(math.log(table_size) / math.log(2.0)))
            f.write("    {{{:s}_fxpt_lutable_{:d}, {:d}, {:d}, {:d}, {:f}}},\n".format(
                variable_base, i, table_size, log_2_table_size,
                lutset[i].highest_harmonic, scale_factors[i]))
        # Final entry is null to indicate end of table.
        f.write("    {NULL, 0, 0, 0, 0.0},\n")
        f.write("};\n")
        f.write("\n")
        f.write("#endif // LUTSET_x_DEFINED\n")
    print("wrote", filename)


def make_log2_exp2_luts(filename):
    """Write the fixed-point exp2 and log2 lookup tables."""
    variable_base = 'exp_lut'
    with open(filename, "w") as f:
        f.write("// Automatically-generated LUTset\n")
        f.write("#ifndef LUTSET_{:s}_FXPT_DEFINED\n".format(variable_base.upper()))
        f.write("#define LUTSET_{:s}_FXPT_DEFINED\n".format(variable_base.upper()))
        f.write("\n")
        # Define the content of the individual tables.
        write_fxpt_lutable(f, create_log2_lut(257), 'log2_fxpt_lutable')
        write_fxpt_lutable(f, create_exp2_lut(257), 'exp2_fxpt_lutable')
        f.write("\n")
        f.write("#endif // LUTSET_x_DEFINED\n")
    print("wrote", filename)


def make_clipping_lut(filename):
    # Soft clipping lookup table scratchpad.
    SAMPLE_MAX = 32767
    linear_proportion = 0.9  # I tried 0.6 and you could hear the difference but not enough to matter.
    LIN_MAX = int(round(linear_proportion * 32768))  # 29491
    NONLIN_RANGE = round(1.5 * (32767 - LIN_MAX))  # size of nonlinearity lookup table = 4915

    clipping_lookup_table = np.arange(LIN_MAX + NONLIN_RANGE)

    for x in range(NONLIN_RANGE):
        x_dash = float(x) / NONLIN_RANGE
        clipping_lookup_table[x + LIN_MAX] = LIN_MAX + int(np.floor(NONLIN_RANGE * (x_dash - x_dash * x_dash * x_dash / 3.0)))

    with open(filename, "w") as f:
        f.write("// Automatically generated.\n// Clipping lookup table\n")
        f.write("#ifndef __CLIPPING_TABLE\n#define __CLIPPING_TABLE\n")
        f.write("#define FIRST_NONLIN %d\n" % LIN_MAX)
        f.write("#define NONLIN_RANGE %d\n" % NONLIN_RANGE)
        f.write("// First sample value beyond end of table (just clip to max).\n")
        f.write("#define FIRST_HARDCLIP (FIRST_NONLIN + NONLIN_RANGE)\n")
        f.write("const uint16_t clipping_lookup_table[NONLIN_RANGE] PROGMEM = {\n")
        samples_per_row = 8
        for row_start in range(0, NONLIN_RANGE, samples_per_row):
            for sample in range(row_start, min(NONLIN_RANGE, row_start + samples_per_row)):
                f.write("%d," % clipping_lookup_table[LIN_MAX + sample])
            f.write("\n")
        f.write("};\n")
        f.write("#endif\n")
    print("wrote", filename)

def make_patches(filename):
    def nothing(**kwargs):
        return

    import juno, amy, fm
    num_oscs =[]
    # Don't make any noise
    amy.override_send = nothing

    with open(filename, "w") as f:
        f.write("// Automatically generated.\n// DX7 and juno 106 patch table\n")
        f.write("#ifndef __PATCHESH\n#define __PATCHESH\n")
        f.write("const char * patch_commands[256] PROGMEM = {\n")
        # Do juno
        for i in range(128):
            amy.log_patch()
            p = juno.JunoPatch()
            j = p.from_patch_number(i)
            j.base_oscs = list()
            v = j.get_new_voices(1)
            f.write("\t/* %d: Juno %s */ \"%s\",\n" % (i, j.name, amy.retrieve_patch()))  
            num_oscs.append(5)
        # Do dx7
        for i in range(128):
            amy.log_patch()
            p = fm.AMYPatch.from_dx7(fm.DX7Patch.from_patch_number(i))
            p.send_to_AMY(reset=False)
            f.write("\t/* %d: DX7 %s */ \"%s\",\n" % (i+128, p.name, amy.retrieve_patch()))  
            num_oscs.append(9)
        f.write("};\n")
        f.write("const uint16_t patch_oscs[256] PROGMEM = {\n")
        for i in num_oscs:
            f.write("%d," % (i))
        f.write("\n};\n#endif\n")
    amy.override_send = None


""" 
    Generate all the headers except for the partials headers
"""
def generate_all():
    import fm
    import collections
    # Implement the multiple lookup tables.
    # A LUT is stored as an array of values (table) and the harmonic number of the
    # highest harmonic they contain (i.e., the number of cycles it completes in the
    # entire table, so must be <= len(table)/2.)
    LUTentry = collections.namedtuple('LUTentry', ['table', 'highest_harmonic'])

    # Impulses.
    #impulse_lutset = create_lutset(LUTentry, np.ones(128))
    ##write_lutset_to_h('src/impulse_lutset.h', 'impulse', impulse_lutset)
    #write_lutset_to_h_as_fxpt('src/impulse_lutset_fxpt.h', 'impulse', impulse_lutset)

    # Saw_up.
    saw_lutset = create_lutset(LUTentry, [0] + list(-1 / np.arange(1, 256)), -np.pi/2 * np.ones(256))
    #write_lutset_to_h('src/saw_lutset.h', 'saw', saw_lutset)
    write_lutset_to_h_as_fxpt('src/saw_lutset_fxpt.h', 'saw', saw_lutset)

    # Triangle wave lutset
    n_harms = 64
    coefs = (np.arange(n_harms) % 2) * (
        np.maximum(1, np.arange(n_harms, dtype=float))**(-2))
    triangle_lutset = create_lutset(LUTentry, coefs, np.arange(len(coefs)) * -np.pi / 2)
    #write_lutset_to_h('src/triangle_lutset.h', 'triangle', triangle_lutset)
    write_lutset_to_h_as_fxpt('src/triangle_lutset_fxpt.h', 'triangle', triangle_lutset)

    # Sinusoid "lutset" (only one table)
    sine_lutset = create_lutset(LUTentry, np.array([0, 1]),  harmonic_phases = -np.pi / 2 * np.ones(2), length_factor=256)
    #write_lutset_to_h('src/sine_lutset.h', 'sine', sine_lutset)
    write_lutset_to_h_as_fxpt('src/sine_lutset_fxpt.h', 'sine', sine_lutset)

    # log2/exp2 LUTs
    make_log2_exp2_luts('src/log2_exp2_fxpt_lutable.h')

    # Clipping LUT
    make_clipping_lut('src/clipping_lookup_table.h')

    # PCM LUT
    generate_both_pcm_headers()

    # Juno & FM patches
    make_patches("src/patches.h")


def main():
    print("Generating all headers needed for AMY (except for partials patches, see partials.py if you want to DIY...")
    generate_all()
    print("Done.")

if __name__ == '__main__':
    main()






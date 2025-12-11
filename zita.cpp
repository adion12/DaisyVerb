#include "daisy_versio.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyVersio hw;

#define MAX_ORD 16U

// Matrix scalar for Hadamard
float matrix_scalar = 0.5f;

// FDN feedback matrix
const signed char hadamard_matrix[MAX_ORD][MAX_ORD] = {
    { 1,  1,  1,  1,  1,  1,  1,  1,    1,  1,  1,  1,  1,  1,  1,  1 },
    { 1, -1,  1, -1,  1, -1,  1, -1,    1, -1,  1, -1,  1, -1,  1, -1 },
    { 1,  1, -1, -1,  1,  1, -1, -1,    1,  1, -1, -1,  1,  1, -1, -1 },
    { 1, -1, -1,  1,  1, -1, -1,  1,    1, -1, -1,  1,  1, -1, -1,  1 },
    { 1,  1,  1,  1, -1, -1, -1, -1,    1,  1,  1,  1, -1, -1, -1, -1 },
    { 1, -1,  1, -1, -1,  1, -1,  1,    1, -1,  1, -1, -1,  1, -1,  1 },
    { 1,  1, -1, -1, -1, -1,  1,  1,    1,  1, -1, -1, -1, -1,  1,  1 },
    { 1, -1, -1,  1, -1,  1,  1, -1,    1, -1, -1,  1, -1,  1,  1, -1 },

    { 1,  1,  1,  1,  1,  1,  1,  1,   -1, -1, -1, -1, -1, -1, -1, -1 },
    { 1, -1,  1, -1,  1, -1,  1, -1,   -1,  1, -1,  1, -1,  1, -1,  1 },
    { 1,  1, -1, -1,  1,  1, -1, -1,   -1, -1,  1,  1, -1, -1,  1,  1 },
    { 1, -1, -1,  1,  1, -1, -1,  1,   -1,  1,  1, -1, -1,  1,  1, -1 },
    { 1,  1,  1,  1, -1, -1, -1, -1,   -1, -1, -1, -1,  1,  1,  1,  1 },
    { 1, -1,  1, -1, -1,  1, -1,  1,   -1,  1, -1,  1,  1, -1,  1, -1 },
    { 1,  1, -1, -1, -1, -1,  1,  1,   -1, -1,  1,  1,  1,  1, -1, -1 },
    { 1, -1, -1,  1, -1,  1,  1, -1,   -1,  1,  1, -1,  1, -1, -1,  1 }
};

const float primes[MAX_ORD] = {2.f, 3.f, 5.f, 7.f, 11.f, 13.f, 17.f, 19.f, 23.f, 29.f, 31.f, 37.f, 41.f, 43.f, 47.f, 53.f};

const float FDN_lengths_tuning[MAX_ORD] = {7351.f, 10099.f, 6133.f, 12329.f,   9927.f, 6007.f, 10559.f, 8387.f,   9011.f, 8501.f, 11003.f, 7207.f,   12011.f, 7001.f, 10069.f, 6491.f};
const float APF_lengths_tuning[MAX_ORD] = {977.f, 1171.f, 1511.f, 1307.f,      1409.f, 647.f, 919.f, 1097.f,      1061.f, 1423.f, 1297.f, 863.f,     1327.f, 599.f, 1481.f, 757.f};

float FDN_lengths[MAX_ORD] = {7351.f, 10099.f, 6133.f, 12329.f,   9927.f, 6007.f, 10559.f, 8387.f,   9011.f, 8501.f, 11003.f, 7207.f,   12011.f, 7001.f, 10069.f, 6491.f};
float APF_lengths[MAX_ORD] = {977.f, 1171.f, 1511.f, 1307.f,      1409.f, 647.f, 919.f, 1097.f,      1061.f, 1423.f, 1297.f, 863.f,     1327.f, 599.f, 1481.f, 757.f};

// Feeback Delay Network DelayLines
DSY_SDRAM_BSS DelayLine<float, 12329U + 1U> FDN_del[MAX_ORD];

// All-Pass Filter DelayLines
DelayLine<float, 1511U + 1U> APF_del[MAX_ORD];

float sig_in;
float sig_out[2];
float APF_y[MAX_ORD];
float FDN_feedback, APF_feedback;
float APF_input, APF_del_out;
uint16_t r, c;

float t60_hfs = 3.0f; // t60 for half sample rate (f = fs/2 Hz)
float t60_dc  = 3.0f; // t60 for DC (f = 0 Hz)
float fs = 48000.0f;

float D, R_hfs, R_dc; // LPF
float lpf[MAX_ORD], p[MAX_ORD], g[MAX_ORD];

// UI
float APF_g = 0.6f; // All-Pass Filter Gain
float out_gain = 0.5;
bool freeze = 0;
uint8_t order = 4;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		sig_in = 0.5f * (in[0][i] + in[1][i]) * out_gain; // Mono

		// Process APF outputs
		for(r = 0; r < order; r++){
			// One Multiply APF
            APF_del_out = APF_del[r].Read(APF_lengths[r]);
			APF_input = sig_in + FDN_del[r].Read(FDN_lengths[r]);
			APF_feedback = APF_g * (APF_del_out - APF_input);
			APF_del[r].Write(APF_feedback + APF_input);
			APF_y[r] = APF_feedback + APF_del_out;
        }

		// Process FDN
		sig_out[0] = 0.0f;
		sig_out[1] = 0.0f;
        for(r = 0; r < order; r++){
			FDN_feedback = 0.0f;
			for(c = 0; c < order; c++){
                FDN_feedback += hadamard_matrix[r][c] * APF_y[c] * matrix_scalar;
            }
			sig_out[r % 2] += FDN_feedback;
			
			lpf[r] = (g[r] * FDN_feedback) + (p[r] * lpf[r]); 
            FDN_del[r].Write(lpf[r]);
        }


		out[0][i] = (sig_out[0] * matrix_scalar) + ((1.0f - out_gain) * in[0][i]);
		out[1][i] = (sig_out[1] * matrix_scalar) + ((1.0f - out_gain) * in[1][i]);
	}
}

int main(void)
{
	hw.Init();
	hw.SetAudioBlockSize(4);
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

	// Initialize & set elements
    for(r = 0; r < MAX_ORD; r++){
        FDN_del[r].Init();
        FDN_del[r].SetDelay(12329U);
		APF_del[r].Init();
        APF_del[r].SetDelay(1511U);
		lpf[r] = 0.0f;
    }

	// LEDs
	for (r = 0; r < 4; r++)
	{
		hw.SetLed(r, freeze, !freeze, 0);
	}
	hw.UpdateLeds();

	// Toggle Switch
	uint8_t order_state_n, order_state = 3;
	
	float vary_length = 1.0f;
	float new_length;
	float prime_i;
	uint8_t multiplicity;

	hw.StartAdc();
	hw.StartAudio(AudioCallback);

	while(1) {
		// Pots
		hw.ProcessAnalogControls(); // Normalize CV inputs
		out_gain = hw.GetKnobValue(0);
		APF_g = (0.6f * hw.GetKnobValue(3));
		
		// Toggle Switch
		order_state_n = hw.sw[1].Read();
		if (order_state != order_state_n) {
			order_state = order_state_n;
			switch (order_state) {
				case 1: // Left
					order = 4U;
					matrix_scalar = 0.5f;
					break;
				case 0: // Center
					order = 8U;
					matrix_scalar = 0.35355339059327373f; // 1/sqrt(8)
					break;
				case 2: // Right
					order = 16U;
					matrix_scalar = 0.25f;
					break;
			}
		}

		vary_length = (0.8f * hw.GetKnobValue(4)) + 0.2f; // [0.2, 1.0]
		if (vary_length > 0.99f) {
			for (uint8_t idx = 0; idx < order; idx++) {
				FDN_lengths[idx] = FDN_lengths_tuning[idx];
				APF_lengths[idx] = APF_lengths_tuning[idx];
			}
		} else {
			for (uint8_t idx = 0; idx < order; idx++) {
				prime_i = primes[idx];

				new_length = FDN_lengths_tuning[idx] * vary_length;
				multiplicity = (uint8_t) (0.5f + (log2f(new_length) / log2f(prime_i)));
				FDN_lengths[idx] = powf(prime_i, multiplicity);

				new_length = APF_lengths_tuning[idx] * vary_length;
				multiplicity = (uint8_t) (0.5f + (log2f(new_length) / log2f(prime_i)));
				APF_lengths[idx] = powf(prime_i, multiplicity);
			}
		}

		// Button 
		hw.tap.Debounce();
		if (hw.SwitchPressed()) {
			freeze = !freeze;
			for (uint8_t idx = 0; idx < 4; idx++) {
				hw.SetLed(idx, freeze, !freeze, 0);
			}
			hw.UpdateLeds();
			System::Delay(200);
		}

		// LPF
		t60_dc  = (5.0f * hw.GetKnobValue(1)) + 0.5f;
		t60_hfs = (5.0f * hw.GetKnobValue(2)) + 0.5f;
		for (uint8_t idx = 0; idx < order; idx++) {
			D = -3.0f * FDN_lengths[idx] / fs;
			R_dc = pow10f(D / t60_dc);
			R_hfs = pow10f(D / t60_hfs);
			g[idx] = (2.0f * R_dc * R_hfs) / (R_dc + R_hfs);
			if (freeze) { p[idx] = 1.0f - g[idx]; }
			else        { p[idx] = (R_dc - R_hfs) / (R_dc + R_hfs); }
		}

	}
}

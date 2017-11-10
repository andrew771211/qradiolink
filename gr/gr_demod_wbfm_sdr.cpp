// Written by Adrian Musceac YO8RZZ , started March 2016.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "gr_demod_wbfm_sdr.h"

gr_demod_wbfm_sdr::gr_demod_wbfm_sdr(gr::qtgui::sink_c::sptr fft_gui,
                                     gr::qtgui::const_sink_c::sptr const_gui,
                                     gr::qtgui::number_sink::sptr rssi_gui, QObject *parent,
                                     int samp_rate, int carrier_freq, int filter_width,
                                     float mod_index, float device_frequency, float rf_gain,
                                     std::string device_args, std::string device_antenna, int freq_corr) :
    QObject(parent)
{
    _msg_nr = 0;
    _target_samp_rate = 200000;
    _rssi = rssi_gui;
    _device_frequency = device_frequency;
    _samp_rate = samp_rate;
    _carrier_freq = carrier_freq;
    _filter_width = filter_width;
    _modulation_index = mod_index;
    _top_block = gr::make_top_block("wbfm demodulator sdr");

    float rerate = 8000.0/(float)_target_samp_rate;

    unsigned int flt_size = 32;

    static const float coeff[] = {8.23112713987939e-05, 0.30221322178840637,
                                  1.3954089879989624, 0.302213191986084, 8.23112713987939e-05};
    std::vector<float> iir_taps(coeff, coeff + sizeof(coeff) / sizeof(coeff[0]) );
    _deemphasis_filter = gr::filter::fft_filter_fff::make(1,iir_taps);

    std::vector<float> taps = gr::filter::firdes::low_pass(1, _samp_rate, _filter_width, 12000);
    std::vector<float> audio_taps = gr::filter::firdes::low_pass(1, _target_samp_rate, 4000, 600);
    _resampler = gr::filter::rational_resampler_base_ccf::make(1,5,taps);
    _audio_resampler = gr::filter::pfb_arb_resampler_fff::make(rerate, audio_taps, flt_size);
    _signal_source = gr::analog::sig_source_c::make(_samp_rate,gr::analog::GR_COS_WAVE,-200000,1);
    _multiply = gr::blocks::multiply_cc::make();
    _filter = gr::filter::fft_filter_ccf::make(1, gr::filter::firdes::low_pass(
                            1, _target_samp_rate, _filter_width,600,gr::filter::firdes::WIN_HAMMING) );

    _audio_sink = make_gr_audio_sink();
    _pilot_filter = gr::filter::fft_filter_fff::make(1,gr::filter::firdes::band_reject(
                                                         2.0,_target_samp_rate,18000,20000,
                                                         500,gr::filter::firdes::WIN_BLACKMAN_HARRIS));
    _fm_demod = gr::analog::quadrature_demod_cf::make(_target_samp_rate/(2*M_PI* _filter_width));
    _squelch = gr::analog::pwr_squelch_cc::make(-140,0.01,0,true);
    _amplify = gr::blocks::multiply_const_ff::make(5);

    _message_sink = gr::blocks::message_debug::make();

    _rssi_valve = gr::blocks::copy::make(8);
    _rssi_valve->set_enabled(false);
    _fft_valve = gr::blocks::copy::make(8);
    _mag_squared = gr::blocks::complex_to_mag_squared::make();
    _single_pole_filter = gr::filter::single_pole_iir_filter_ff::make(0.04);
    _log10 = gr::blocks::nlog10_ff::make();
    _multiply_const_ff = gr::blocks::multiply_const_ff::make(10);
    _moving_average = gr::blocks::moving_average_ff::make(25000,1,2000);
    _add_const = gr::blocks::add_const_ff::make(-50);


    _osmosdr_source = osmosdr::source::make(device_args);
    _osmosdr_source->set_center_freq(_device_frequency - 200000.0);
    _osmosdr_source->set_sample_rate(_samp_rate);
    _osmosdr_source->set_freq_corr(freq_corr);
    _osmosdr_source->set_gain_mode(false);
    _osmosdr_source->set_antenna(device_antenna);
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();

    if (!range.empty())
    {
        double gain =  range.start() + rf_gain*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
    else
    {
        _osmosdr_source->set_gain_mode(true);
    }

    _constellation = const_gui;
    _fft_gui = fft_gui;
    _top_block->connect(_osmosdr_source,0,_multiply,0);
    _top_block->connect(_signal_source,0,_multiply,1);
    _top_block->connect(_multiply,0,_resampler,0);
    _top_block->connect(_multiply,0,_fft_valve,0);
    _top_block->connect(_fft_valve,0,_fft_gui,0);
    _top_block->msg_connect(_fft_gui,"freq",_message_sink,"store");
    _top_block->connect(_resampler,0,_filter,0);
    _top_block->connect(_filter,0,_squelch,0);
    _top_block->connect(_squelch,0,_fm_demod,0);
    _top_block->connect(_fm_demod,0,_pilot_filter,0);
    _top_block->connect(_pilot_filter,0,_amplify,0);
    _top_block->connect(_amplify,0,_audio_resampler,0);
    _top_block->connect(_audio_resampler,0,_audio_sink,0);


    _top_block->connect(_filter,0,_rssi_valve,0);
    _top_block->connect(_rssi_valve,0,_mag_squared,0);
    _top_block->connect(_mag_squared,0,_moving_average,0);
    _top_block->connect(_moving_average,0,_single_pole_filter,0);
    _top_block->connect(_single_pole_filter,0,_log10,0);
    _top_block->connect(_log10,0,_multiply_const_ff,0);
    _top_block->connect(_multiply_const_ff,0,_add_const,0);
    _top_block->connect(_add_const,0,_rssi,0);
}

void gr_demod_wbfm_sdr::start()
{
    _top_block->start();
}

void gr_demod_wbfm_sdr::stop()
{
    _top_block->stop();
    _top_block->wait();
}

std::vector<float>* gr_demod_wbfm_sdr::getData()
{
    std::vector<float> *data = _audio_sink->get_data();
    return data;
}

void gr_demod_wbfm_sdr::tune(long center_freq)
{
    _device_frequency = center_freq;
    _osmosdr_source->set_center_freq(_device_frequency-25000);
}

double gr_demod_wbfm_sdr::get_freq()
{
    int n = _message_sink->num_messages();
    if(n > _msg_nr)
    {
        _msg_nr = n;
        pmt::pmt_t msg = _message_sink->get_message(n - 1);
        return pmt::to_double(pmt::cdr(msg));
    }
    else
    {
        return 0;
    }
}

void gr_demod_wbfm_sdr::set_rx_sensitivity(float value)
{
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();
    if (!range.empty())
    {
        double gain =  range.start() + value*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
}

void gr_demod_wbfm_sdr::enable_gui_const(bool value)
{
    _rssi_valve->set_enabled(value);
}

void gr_demod_wbfm_sdr::enable_gui_fft(bool value)
{
    _fft_valve->set_enabled(value);
}

void gr_demod_wbfm_sdr::set_squelch(int value)
{
    _squelch->set_threshold(value);
}

/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/ring_buffer.h"
#include "wivrn_packets.h"
#include <atomic>

struct AAudioStreamStruct;
class wivrn_session;

namespace xr
{
class instance;
}

namespace wivrn::android
{
class audio
{
	static int32_t speaker_data_cb(AAudioStreamStruct*, void*, void*, int32_t);
	static int32_t microphone_data_cb(AAudioStreamStruct*, void*, void*, int32_t);

	utils::ring_buffer<xrt::drivers::wivrn::audio_data, 1000> output_buffer;

	xrt::drivers::wivrn::audio_data speaker_tmp;
	AAudioStreamStruct * speaker = nullptr;
	std::atomic<bool> speaker_stop_ack = false;
	AAudioStreamStruct * microphone = nullptr;
	std::atomic<bool> microphone_stop_ack = false;

	wivrn_session & session;
	xr::instance & instance;

	std::atomic<bool> exiting = false;

	void exit();
public:
	audio(const audio &) = delete;
	audio & operator=(const audio &) = delete;
	audio(const xrt::drivers::wivrn::to_headset::audio_stream_description &, wivrn_session &, xr::instance &);
	~audio();

	void operator()(xrt::drivers::wivrn::audio_data &&);

	static void get_audio_description(xrt::drivers::wivrn::from_headset::headset_info_packet & info);
};
} // namespace wivrn::android

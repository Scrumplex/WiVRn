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

#include "decoder/shard_accumulator.h"
#include "scene.h"
#include "stream_reprojection.h"
#include "utils/sync_queue.h"
#include "wivrn_client.h"
#include "wivrn_packets.h"
#include <mutex>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "audio/audio.h"
#include "render/imgui_impl.h"

namespace scenes
{
class stream : public scene_impl<stream>, public std::enable_shared_from_this<stream>
{
public:
	enum class state
	{
		initializing,
		streaming,
		stalled
	};

private:
	static const size_t view_count = 2;

	using stream_description = xrt::drivers::wivrn::to_headset::video_stream_description::item;

	struct accumulator_images
	{
		std::unique_ptr<shard_accumulator> decoder;
		vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
		vk::DescriptorSet descriptor_set = nullptr;
		vk::raii::PipelineLayout blit_pipeline_layout = nullptr;
		vk::raii::Pipeline blit_pipeline = nullptr;
		// latest frames from oldest to most recent
		std::array<std::shared_ptr<shard_accumulator::blit_handle>, 3> latest_frames;

		static std::optional<uint64_t> common_frame(const std::vector<accumulator_images> &, uint64_t preferred_index);
		std::shared_ptr<shard_accumulator::blit_handle> frame(std::optional<uint64_t> id);
		std::vector<uint64_t> frames() const;
	};

	struct renderpass_output
	{
		vk::Extent2D size;
		vk::Format format;
		image_allocation image;
		vk::raii::ImageView image_view = nullptr;
		vk::raii::Framebuffer frame_buffer = nullptr;
	};

	std::unique_ptr<wivrn_session> network_session;
	std::atomic<bool> exiting = false;
	std::thread network_thread;
	std::optional<std::thread> tracking_thread;
	std::thread video_thread;

	utils::sync_queue<to_headset::video_stream_data_shard> shard_queue;

	std::mutex decoder_mutex;
	std::optional<to_headset::video_stream_description> video_stream_description;
	uint64_t next_frame; // Preferred index for next frame
	std::vector<accumulator_images> decoders; // Locked by decoder_mutex
	vk::raii::DescriptorPool blit_descriptor_pool = nullptr;
	vk::raii::RenderPass blit_render_pass = nullptr;

	std::array<renderpass_output, view_count> decoder_output{};

	std::optional<stream_reprojection> reprojector;

	vk::raii::Fence fence = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	std::array<std::pair<XrAction, XrPath>, 2> haptics_actions;
	std::vector<std::tuple<device_id, XrAction, XrActionType>> input_actions;

	state state_ = state::initializing;
	XrTime first_frame_time{};
	const float dbrightness = 2;
	bool show_performance_metrics = false;

	std::vector<xr::swapchain> swapchains;
	xr::swapchain swapchain_imgui;
	vk::Format swapchain_format;

	std::optional<audio> audio_handle;

	std::optional<imgui_context> imgui_ctx;

	// Keep a reference to the resources needed to blit the images until vkWaitForFences
	std::vector<std::shared_ptr<shard_accumulator::blit_handle>> current_blit_handles;

	stream() = default;

public:
	~stream();

	static std::shared_ptr<stream> create(std::unique_ptr<wivrn_session> session, bool show_performance_metrics, bool enable_microphone);

	void render(XrTime predicted_display_time, bool should_render) override;
	void on_focused() override;
	void on_unfocused() override;

	void operator()(to_headset::handshake&&) {};
	void operator()(to_headset::video_stream_data_shard &&);
	void operator()(to_headset::haptics &&);
	void operator()(to_headset::timesync_query &&);
	void operator()(to_headset::audio_stream_description &&);
	void operator()(to_headset::video_stream_description &&);
	void operator()(audio_data&&);

	void push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle);

	void send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback);

	state current_state() const
	{
		return state_ ;
	}

	bool alive() const
	{
		return !exiting;
	}

	static meta& get_meta_scene();

private:
	void process_packets();
	void tracking();
	void video();
	void read_actions();

	void setup(const to_headset::video_stream_description &);
	void setup_reprojection_swapchain();
	void exit();

	vk::raii::QueryPool query_pool = nullptr;
	bool query_pool_filled = false;

	uint64_t bytes_sent = 0;
	uint64_t bytes_received = 0;
	float bandwidth_rx = 0;
	float bandwidth_tx = 0;

	struct gpu_timestamps
	{
		float gpu_barrier = 0;
		float gpu_time = 0;
	};

	struct global_metric //: gpu_timestamps
	{
		float gpu_barrier;
		float gpu_time;
		float cpu_time = 0;
		float bandwidth_rx = 0;
		float bandwidth_tx = 0;

	};

	struct plot
	{
		const char * title;
		struct subplot
		{
			const char * title;
			float scenes::stream::global_metric::*data;
		};
		std::vector<subplot> subplots;
		const char * unit;
	};

	static const inline int size_gpu_timestamps = 1 + sizeof(gpu_timestamps) / sizeof(float);

	struct decoder_metric
	{
		// All times are in seconds relative to encode_begin
		float send_begin;
		float send_end;
		float received_first_packet;
		float received_last_packet;
		float sent_to_decoder;
		float received_from_decoder;
		float blitted;
		float displayed;
	};

	std::vector<global_metric> global_metrics{300};
	std::vector<std::vector<decoder_metric>> decoder_metrics;
	std::vector<float> axis_scale;
	XrTime last_metric_time = 0;
	int metrics_offset = 0;

	void accumulate_metrics(XrTime predicted_display_time, const std::vector<std::shared_ptr<shard_accumulator::blit_handle>>& blit_handles, const gpu_timestamps& timestamps);
	XrCompositionLayerQuad plot_performance_metrics(XrTime predicted_display_time);
};
} // namespace scenes

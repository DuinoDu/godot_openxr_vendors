/**************************************************************************/
/*  openxr_pico_secure_mr.cpp                                            */
/**************************************************************************/
/*                       This file is part of:                            */
/*                              GODOT XR                                  */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Copyright (c) 2022-present Godot XR contributors (see CONTRIBUTORS.md) */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "classes/openxr_pico_secure_mr.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "extensions/openxr_pico_secure_mr_extension_wrapper.h"

using namespace godot;

namespace {
const std::chrono::milliseconds kDefaultReadbackIntervalMs(33);

size_t _tensor_data_type_stride(int32_t data_type) {
    switch (data_type) {
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO:
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT8_PICO:
            return 1;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT16_PICO:
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT16_PICO:
            return 2;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO:
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO:
            return 4;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT64_PICO:
            return 8;
        default:
            return 0;
    }
}
} // namespace

struct OpenXRPicoSecureMR::TensorReadbackWorker {
    struct Target {
        uint64_t tensor = 0;
        String name;
        std::vector<int32_t> dimensions;
        int32_t channels = 0;
        int32_t data_type = XR_SECURE_MR_TENSOR_DATA_TYPE_MAX_ENUM_PICO;

        size_t estimate_payload_size() const {
            const size_t stride = _tensor_data_type_stride(data_type);
            if (stride == 0) {
                return 0;
            }
            size_t total = stride * static_cast<size_t>(std::max(1, channels));
            for (int32_t dim : dimensions) {
                if (dim <= 0) {
                    return 0;
                }
                const size_t dim_size = static_cast<size_t>(dim);
                if (total > std::numeric_limits<size_t>::max() / dim_size) {
                    return 0;
                }
                total *= dim_size;
            }
            return total;
        }
    };

    struct TargetState {
        Target target;
        bool in_flight = false;
    };

    struct PendingFuture {
        TargetState *state = nullptr;
        XrFutureEXT future = XR_NULL_HANDLE;
    };

    struct Result {
        String name;
        uint64_t tensor = 0;
        std::vector<uint8_t> data;
        std::vector<int32_t> dimensions;
        int32_t channels = 0;
        int32_t data_type = XR_SECURE_MR_TENSOR_DATA_TYPE_MAX_ENUM_PICO;
        XrResult future_result = XR_SUCCESS;
    };

    TensorReadbackWorker(OpenXRPicoReadbackTensorExtensionWrapper *in_wrapper, std::vector<Target> targets, int32_t polling_interval_ms) :
            readback_wrapper(in_wrapper) {
        if (polling_interval_ms <= 0) {
            polling_interval_ms = 1;
        }
        polling_interval = std::chrono::milliseconds(polling_interval_ms);
        targets_.reserve(targets.size());
        for (auto &target : targets) {
            if (target.tensor == 0) {
                continue;
            }
            TargetState state;
            state.target = std::move(target);
            state.in_flight = false;
            targets_.push_back(std::move(state));
        }
    }

    ~TensorReadbackWorker() {
        stop();
    }

    void start() {
        if (targets_.empty() || readback_wrapper == nullptr) {
            return;
        }
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) {
            return;
        }
        worker = std::thread(&TensorReadbackWorker::loop, this);
    }

    void stop() {
        bool was_running = running.exchange(false);
        if (!was_running) {
            return;
        }
        state_cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        pending_futures.clear();
        for (auto &state : targets_) {
            state.in_flight = false;
        }
    }

    bool is_running() const {
        return running.load(std::memory_order_acquire);
    }

    std::vector<Result> pop_results() {
        std::lock_guard<std::mutex> lock(results_mutex);
        std::vector<Result> out;
        out.swap(completed_results);
        return out;
    }

private:
    void loop() {
        auto next_schedule = std::chrono::steady_clock::now();
        while (running.load(std::memory_order_acquire)) {
            UtilityFunctions::print("[SecureMRReadback] loop once start");
            if (pending_futures.empty()) {
                auto now = std::chrono::steady_clock::now();
                if (now < next_schedule) {
                    std::unique_lock<std::mutex> lock(state_mutex);
                    state_cv.wait_until(lock, next_schedule, [this]() { return !running.load(std::memory_order_acquire); });
                    continue;
                }
            }
            UtilityFunctions::print("[SecureMRReadback] loop once 1");

            if (!running.load(std::memory_order_acquire)) {
                break;
            }
            UtilityFunctions::print("[SecureMRReadback] loop once 2");

            auto now = std::chrono::steady_clock::now();
            if (now >= next_schedule) {
                schedule_futures();
                next_schedule = std::chrono::steady_clock::now() + polling_interval;
            }
            UtilityFunctions::print("[SecureMRReadback] loop once 3");

            while (running.load(std::memory_order_acquire) && process_single_future()) {
            }
            UtilityFunctions::print("[SecureMRReadback] loop once end");
        }

        pending_futures.clear();
        for (auto &state : targets_) {
            state.in_flight = false;
        }
    }

    void schedule_futures() {
        UtilityFunctions::print("[SecureMRReadback] schedule_futures start");
        for (auto &state : targets_) {
            if (!running.load(std::memory_order_acquire)) {
                return;
            }
            if (state.in_flight) {
                continue;
            }

            while (pending_futures.size() >= kMaxQueueDepth && running.load(std::memory_order_acquire)) {
                if (!process_single_future()) {
                    break;
                }
            }

            if (pending_futures.size() >= kMaxQueueDepth) {
                break;
            }

            enqueue_future(state);
        }
        UtilityFunctions::print("[SecureMRReadback] schedule_futures end");
    }

    bool enqueue_future(TargetState &state) {
        UtilityFunctions::print("[SecureMRReadback] enqueue_future start");
        if (state.in_flight || state.target.tensor == 0 || readback_wrapper == nullptr) {
            return false;
        }

        XrSecureMrTensorPICO tensor_handle = (XrSecureMrTensorPICO)state.target.tensor;
        XrFutureEXT future = XR_NULL_HANDLE;
        XrResult result = readback_wrapper->xrCreateBufferFromGlobalTensorAsyncPICO(tensor_handle, &future);
        if (XR_FAILED(result) || future == XR_NULL_HANDLE) {
            UtilityFunctions::printerr("[SecureMRReadback] xrCreateBufferFromGlobalTensorAsyncPICO failed for ", state.target.name, " (result=", (int)result, ")");
            return false;
        }

        PendingFuture pending;
        pending.state = &state;
        pending.future = future;
        pending_futures.push_back(pending);
        state.in_flight = true;
        UtilityFunctions::print("[SecureMRReadback] enqueue_future end");
        return true;
    }

    bool process_single_future() {
        UtilityFunctions::print("[SecureMRReadback] process_single_future start");
        if (pending_futures.empty()) {
            return false;
        }

        PendingFuture pending = pending_futures.front();
        pending_futures.pop_front();

        if (pending.state == nullptr) {
            return true;
        }

        if (!running.load(std::memory_order_acquire)) {
            pending.state->in_flight = false;
            return false;
        }

        if (pending.state->target.tensor == 0 || pending.future == XR_NULL_HANDLE) {
            pending.state->in_flight = false;
            return true;
        }

        process_future(*pending.state, pending.future);
        pending.state->in_flight = false;
        UtilityFunctions::print("[SecureMRReadback] process_single_future end");
        return true;
    }

    bool wait_completion(const TargetState &state, XrSecureMrTensorPICO tensor_handle, XrFutureEXT future,
            XrReadbackTensorBufferPICO &buffer, XrCreateBufferFromGlobalTensorCompletionPICO &completion) {
        if (readback_wrapper == nullptr) {
            return XR_ERROR_RUNTIME_FAILURE;
        }
        XrResult last_result;
        while (running.load(std::memory_order_acquire)) {
            completion.type = XR_TYPE_CREATE_BUFFER_FROM_GLOBAL_TENSOR_COMPLETION_PICO;
            completion.next = nullptr;
            completion.futureResult = XR_SUCCESS;
            completion.tensorBuffer = &buffer;

            while (running.load(std::memory_order_acquire)) {
                last_result = readback_wrapper->xrCreateBufferFromGlobalTensorCompletePICO(tensor_handle, future, &completion);
                // if (last_result == XR_SUCCESS) {
                if (last_result == -1) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));

                UtilityFunctions::push_warning(
                    "[SecureMRReadback] xrCreateBufferFromGlobalTensorCompletePICO failed for ",
                    state.target.name, " (result=", (int)last_result, ")");
            }

            UtilityFunctions::push_warning(
                "[SecureMRReadback] xrCreateBufferFromGlobalTensorCompletePICO failed for ",
                state.target.name, " (result=", (int)last_result, ")");
        }
        return false;
    }

    void process_future(TargetState &state, XrFutureEXT future) {
        UtilityFunctions::print("[SecureMRReadback] process_future start");
        XrSecureMrTensorPICO tensor_handle = (XrSecureMrTensorPICO)state.target.tensor;

        XrReadbackTensorBufferPICO buffer = {};
        size_t payload_capacity = state.target.estimate_payload_size();
        if (payload_capacity == 0) {
            UtilityFunctions::printerr("[SecureMRReadback] Unable to determine payload size for ", state.target.name, ". Skipping readback.");
            return;
        }
        if (payload_capacity > std::numeric_limits<uint32_t>::max()) {
            UtilityFunctions::printerr("[SecureMRReadback] Payload for ", state.target.name, " exceeds supported buffer size.");
            return;
        }

        buffer.bufferCapacityInput = static_cast<uint32_t>(payload_capacity);
        buffer.bufferSizeOutput = 0;
        std::vector<uint8_t> payload(payload_capacity);
        buffer.buffer = payload.empty() ? nullptr : payload.data();

        XrCreateBufferFromGlobalTensorCompletionPICO completion = {};
        completion.type = XR_TYPE_CREATE_BUFFER_FROM_GLOBAL_TENSOR_COMPLETION_PICO;
        completion.next = nullptr;
        completion.futureResult = XR_SUCCESS;
        completion.tensorBuffer = &buffer;

        UtilityFunctions::print("[SecureMRReadback] wait_completion_1 start");
        bool completion_result = wait_completion(state, tensor_handle, future, buffer, completion);
        UtilityFunctions::print("[SecureMRReadback] wait_completion_1 end");
        if (completion_result) {
            const size_t required_size = buffer.bufferSizeOutput;
            if (required_size == 0 || required_size > std::numeric_limits<uint32_t>::max()) {
                UtilityFunctions::printerr("[SecureMRReadback] Invalid buffer size reported for ", state.target.name, ".");
                return;
            }
            payload.resize(required_size);
            buffer.bufferCapacityInput = static_cast<uint32_t>(required_size);
            buffer.buffer = payload.data();
            UtilityFunctions::print("[SecureMRReadback] wait_completion_2 start");
            completion_result = wait_completion(state, tensor_handle, future, buffer, completion);
            UtilityFunctions::print("[SecureMRReadback] wait_completion_2 end");
        }

        if (!completion_result) {
            return;
        }

        if (buffer.bufferSizeOutput > buffer.bufferCapacityInput) {
            UtilityFunctions::printerr("[SecureMRReadback] Runtime wrote more bytes than reserved for ", state.target.name, ".");
            return;
        }

        payload.resize(buffer.bufferSizeOutput);

        store_result(state, std::move(payload), completion.futureResult);
        UtilityFunctions::print("[SecureMRReadback] process_future end");
    }

    void store_result(const TargetState &state, std::vector<uint8_t> &&payload, XrResult future_result) {
        UtilityFunctions::print("[SecureMRReadback] store_result start");
        Result result;
        result.name = state.target.name;
        result.tensor = state.target.tensor;
        result.data = std::move(payload);
        result.dimensions = state.target.dimensions;
        result.channels = state.target.channels;
        result.data_type = state.target.data_type;
        result.future_result = future_result;

        std::lock_guard<std::mutex> lock(results_mutex);
        completed_results.push_back(std::move(result));
        UtilityFunctions::print("[SecureMRReadback] store_result end");
    }

    static constexpr size_t kMaxQueueDepth = 100;

    OpenXRPicoReadbackTensorExtensionWrapper *readback_wrapper = nullptr;
    std::vector<TargetState> targets_;
    std::chrono::milliseconds polling_interval = kDefaultReadbackIntervalMs;
    std::atomic<bool> running{false};
    std::thread worker;
    std::deque<PendingFuture> pending_futures;
    std::mutex state_mutex;
    std::condition_variable state_cv;
    std::mutex results_mutex;
    std::vector<Result> completed_results;
};
OpenXRPicoSecureMR *OpenXRPicoSecureMR::singleton = nullptr;

OpenXRPicoSecureMR *OpenXRPicoSecureMR::get_singleton() {
    if (singleton == nullptr) {
        singleton = memnew(OpenXRPicoSecureMR());
    }
    return singleton;
}

OpenXRPicoSecureMR::OpenXRPicoSecureMR() {
    ERR_FAIL_COND_MSG(singleton != nullptr, "An OpenXRPicoSecureMR singleton already exists.");
    wrapper = OpenXRPicoSecureMRExtensionWrapper::get_singleton();
    readback_wrapper = OpenXRPicoReadbackTensorExtensionWrapper::get_singleton();
    singleton = this;
}

OpenXRPicoSecureMR::~OpenXRPicoSecureMR() {
    _stop_all_tensor_readbacks();
    singleton = nullptr;
}

bool OpenXRPicoSecureMR::is_supported() const {
    const bool has_wrapper = wrapper != nullptr;
    const bool ext_supported = has_wrapper ? wrapper->is_secure_mr_supported() : false;
    return has_wrapper && ext_supported;
}

uint64_t OpenXRPicoSecureMR::create_framework(int32_t image_width, int32_t image_height) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_framework(image_width, image_height);
}

void OpenXRPicoSecureMR::destroy_framework(uint64_t framework_handle) {
    if (!wrapper) return;
    wrapper->destroy_framework(framework_handle);
}

uint64_t OpenXRPicoSecureMR::create_pipeline(uint64_t framework_handle) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline(framework_handle);
}

void OpenXRPicoSecureMR::destroy_pipeline(uint64_t pipeline_handle) {
    _release_pipeline_buffers(pipeline_handle);
    if (!wrapper) return;
    wrapper->destroy_pipeline(pipeline_handle);
}

uint64_t OpenXRPicoSecureMR::create_pipeline_tensor_shape(uint64_t pipeline_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline_tensor_shape(pipeline_handle, dimensions, data_type, channels, tensor_type, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_global_tensor_shape(uint64_t framework_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_global_tensor_shape(framework_handle, dimensions, data_type, channels, tensor_type, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_pipeline_tensor_gltf(uint64_t pipeline_handle, const PackedByteArray &buffer, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline_tensor_gltf(pipeline_handle, buffer, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_global_tensor_gltf(uint64_t framework_handle, const PackedByteArray &buffer, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_global_tensor_gltf(framework_handle, buffer, placeholder);
}

void OpenXRPicoSecureMR::reset_pipeline_tensor_bytes(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedByteArray &data) {
    if (!wrapper) return;
    wrapper->reset_pipeline_tensor_bytes(pipeline_handle, tensor_handle, data);
}

void OpenXRPicoSecureMR::reset_pipeline_tensor_floats(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedFloat32Array &data) {
    if (!wrapper) return;
    wrapper->reset_pipeline_tensor_floats(pipeline_handle, tensor_handle, data);
}

uint64_t OpenXRPicoSecureMR::create_operator_basic(uint64_t pipeline_handle, int32_t operator_type) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_basic(pipeline_handle, operator_type);
}

uint64_t OpenXRPicoSecureMR::create_operator_arithmetic(uint64_t pipeline_handle, const String &config_text) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_arithmetic_compose(pipeline_handle, config_text);
}

uint64_t OpenXRPicoSecureMR::create_operator_convert_color(uint64_t pipeline_handle, int32_t convert_code) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_convert_color(pipeline_handle, convert_code);
}

uint64_t OpenXRPicoSecureMR::create_operator_normalize(uint64_t pipeline_handle, int32_t normalize_type) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_normalize(pipeline_handle, normalize_type);
}

uint64_t OpenXRPicoSecureMR::create_operator_model(uint64_t pipeline_handle, const PackedByteArray &model_data, const String &model_name, const String &input_name, const PackedStringArray &output_names, const PackedInt32Array &output_encodings) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_model(pipeline_handle, model_data, model_name, input_name, output_names, output_encodings);
}

void OpenXRPicoSecureMR::set_operator_input_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name) {
    if (!wrapper) return;
    wrapper->set_operator_input_by_name(pipeline_handle, operator_handle, pipeline_tensor_handle, name);
}

void OpenXRPicoSecureMR::set_operator_output_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name) {
    if (!wrapper) return;
    wrapper->set_operator_output_by_name(pipeline_handle, operator_handle, pipeline_tensor_handle, name);
}

void OpenXRPicoSecureMR::set_operator_input_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    if (!wrapper) return;
    wrapper->set_operator_input_by_index(pipeline_handle, operator_handle, pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMR::set_operator_output_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    if (!wrapper) return;
    wrapper->set_operator_output_by_index(pipeline_handle, operator_handle, pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMR::execute_pipeline(uint64_t pipeline_handle, const Array &mappings) {
    if (!wrapper) return;
    wrapper->execute_pipeline(pipeline_handle, mappings);
}

uint64_t OpenXRPicoSecureMR::start_tensor_readback(const Array &targets, int32_t polling_interval_ms) {
    if (targets.is_empty()) {
        UtilityFunctions::printerr("[SecureMRReadback] start_tensor_readback called with no targets.");
        return 0;
    }

    if (readback_wrapper == nullptr) {
        readback_wrapper = OpenXRPicoReadbackTensorExtensionWrapper::get_singleton();
    }

    if (readback_wrapper == nullptr || !readback_wrapper->is_readback_supported()) {
        UtilityFunctions::printerr("[SecureMRReadback] Pico readback tensor extension not available.");
        return 0;
    }

    std::vector<TensorReadbackWorker::Target> parsed_targets;
    parsed_targets.reserve(targets.size());

    for (int i = 0; i < targets.size(); i++) {
        Variant entry = targets[i];
        if (entry.get_type() != Variant::DICTIONARY) {
            UtilityFunctions::printerr("[SecureMRReadback] Target at index ", i, " must be a Dictionary.");
            continue;
        }

        Dictionary dict = entry;
        Variant handle_var = dict.get("global_tensor", Variant());
        if (handle_var.get_type() == Variant::NIL) {
            handle_var = dict.get("global", Variant());
        }
        if (handle_var.get_type() == Variant::NIL) {
            handle_var = dict.get("tensor", Variant());
        }

        uint64_t tensor_handle = 0;
        if (handle_var.get_type() == Variant::INT) {
            tensor_handle = (uint64_t)(int64_t)handle_var;
        }
        if (tensor_handle == 0) {
            UtilityFunctions::printerr("[SecureMRReadback] Target at index ", i, " is missing a valid global tensor handle.");
            continue;
        }

        TensorReadbackWorker::Target target;
        target.tensor = tensor_handle;
        target.name = dict.get("name", String("tensor_") + String::num_int64((int64_t)tensor_handle));

        PackedInt32Array dims_array = dict.get("dimensions", PackedInt32Array());
        target.dimensions.resize(dims_array.size());
        for (int j = 0; j < dims_array.size(); j++) {
            target.dimensions[j] = dims_array[j];
        }

        target.channels = (int32_t)(int64_t)dict.get("channels", (int64_t)0);
        target.data_type = (int32_t)(int64_t)dict.get("data_type", (int64_t)XR_SECURE_MR_TENSOR_DATA_TYPE_MAX_ENUM_PICO);

        parsed_targets.push_back(std::move(target));
    }

    if (parsed_targets.empty()) {
        UtilityFunctions::printerr("[SecureMRReadback] No valid readback targets supplied.");
        return 0;
    }

    if (polling_interval_ms <= 0) {
        polling_interval_ms = (int32_t)kDefaultReadbackIntervalMs.count();
    }

    std::shared_ptr<TensorReadbackWorker> worker = std::make_shared<TensorReadbackWorker>(readback_wrapper, std::move(parsed_targets), polling_interval_ms);
    worker->start();
    if (!worker->is_running()) {
        UtilityFunctions::printerr("[SecureMRReadback] Failed to start readback worker thread.");
        return 0;
    }

    uint64_t handle = 0;
    {
        std::lock_guard<std::mutex> lock(readback_workers_mutex);
        handle = readback_handle_counter++;
        readback_workers[handle] = worker;
    }
    return handle;
}

void OpenXRPicoSecureMR::stop_tensor_readback(uint64_t readback_handle) {
    if (readback_handle == 0) {
        return;
    }

    std::shared_ptr<TensorReadbackWorker> worker;
    {
        std::lock_guard<std::mutex> lock(readback_workers_mutex);
        auto it = readback_workers.find(readback_handle);
        if (it == readback_workers.end()) {
            return;
        }
        worker = it->second;
        readback_workers.erase(it);
    }

    if (worker) {
        worker->stop();
    }
}

Array OpenXRPicoSecureMR::poll_tensor_readback(uint64_t readback_handle) {
    Array out;
    if (readback_handle == 0) {
        return out;
    }

    std::shared_ptr<TensorReadbackWorker> worker;
    {
        std::lock_guard<std::mutex> lock(readback_workers_mutex);
        auto it = readback_workers.find(readback_handle);
        if (it == readback_workers.end()) {
            return out;
        }
        worker = it->second;
    }

    if (!worker) {
        return out;
    }

    std::vector<TensorReadbackWorker::Result> results = worker->pop_results();
    for (auto &result : results) {
        Dictionary entry;
        entry["name"] = result.name;
        entry["global_tensor"] = (uint64_t)result.tensor;

        PackedByteArray payload;
        if (!result.data.empty()) {
            payload.resize(result.data.size());
            UtilityFunctions::print("[SecureMRReadback] memcpy start");
            memcpy(payload.ptrw(), result.data.data(), result.data.size());
            UtilityFunctions::print("[SecureMRReadback] memcpy end");
        }
        entry["data"] = payload;

        PackedInt32Array dims;
        dims.resize(result.dimensions.size());
        for (int i = 0; i < dims.size(); i++) {
            dims.set(i, result.dimensions[i]);
        }
        entry["dimensions"] = dims;
        entry["channels"] = result.channels;
        entry["data_type"] = result.data_type;
        entry["future_result"] = (int32_t)result.future_result;

        out.append(entry);
    }

    return out;
}

void OpenXRPicoSecureMR::set_named_input(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name) {
    if (tensor_handle != 0) {
        wrapper->set_operator_input_by_name(pipeline_handle, operator_handle, tensor_handle, name);
    }
}

void OpenXRPicoSecureMR::set_named_output(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name) {
    if (tensor_handle != 0) {
        wrapper->set_operator_output_by_name(pipeline_handle, operator_handle, tensor_handle, name);
    }
}

void OpenXRPicoSecureMR::do_elementwise(uint64_t pipeline_handle, int32_t op_type, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, op_type);
    set_named_input(pipeline_handle, op, a_tensor, "operand0");
    set_named_input(pipeline_handle, op, b_tensor, "operand1");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

// Convenience ops
void OpenXRPicoSecureMR::op_camera_access(uint64_t pipeline_handle, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO);
    set_named_output(pipeline_handle, op, left_image_tensor, "left image");
    set_named_output(pipeline_handle, op, right_image_tensor, "right image");
    set_named_output(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_output(pipeline_handle, op, camera_matrix_tensor, "camera matrix");
}

void OpenXRPicoSecureMR::op_camera_space_to_world(uint64_t pipeline_handle, uint64_t timestamp_tensor, uint64_t left_transform_tensor, uint64_t right_transform_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO);
    set_named_input(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_output(pipeline_handle, op, left_transform_tensor, "left");
    set_named_output(pipeline_handle, op, right_transform_tensor, "right");
}

void OpenXRPicoSecureMR::op_assignment(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t dst_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO);
    set_named_input(pipeline_handle, op, src_tensor, "src");
    set_named_output(pipeline_handle, op, dst_tensor, "dst");
}

void OpenXRPicoSecureMR::op_arithmetic_compose(uint64_t pipeline_handle, const String &expression, const PackedInt64Array &operand_tensors, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_arithmetic_compose(pipeline_handle, expression);
    for (int i = 0; i < operand_tensors.size(); i++) {
        wrapper->set_operator_input_by_index(pipeline_handle, op, (uint64_t)operand_tensors[i], i);
    }
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_elementwise_min(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_max(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_multiply(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_or(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_and(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_all(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO);
    set_named_input(pipeline_handle, op, operand_tensor, "operand");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_any(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO);
    set_named_input(pipeline_handle, op, operand_tensor, "operand");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_solve_pnp(uint64_t pipeline_handle, uint64_t object_points_tensor, uint64_t image_points_tensor, uint64_t camera_matrix_tensor, uint64_t rot_result_tensor, uint64_t trans_result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO);
    set_named_input(pipeline_handle, op, object_points_tensor, "object points");
    set_named_input(pipeline_handle, op, image_points_tensor, "image points");
    set_named_input(pipeline_handle, op, camera_matrix_tensor, "camera matrix");
    set_named_output(pipeline_handle, op, rot_result_tensor, "rotation");
    set_named_output(pipeline_handle, op, trans_result_tensor, "translation");
}

void OpenXRPicoSecureMR::op_get_affine(uint64_t pipeline_handle, uint64_t src_points_tensor, uint64_t dst_points_tensor, uint64_t result_affine_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO);
    set_named_input(pipeline_handle, op, src_points_tensor, "src");
    set_named_input(pipeline_handle, op, dst_points_tensor, "dst");
    set_named_output(pipeline_handle, op, result_affine_tensor, "result");
}

void OpenXRPicoSecureMR::op_apply_affine(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_image_tensor, uint64_t result_image_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO);
    set_named_input(pipeline_handle, op, affine_tensor, "affine");
    set_named_input(pipeline_handle, op, src_image_tensor, "src image");
    set_named_output(pipeline_handle, op, result_image_tensor, "dst image");
}

void OpenXRPicoSecureMR::op_apply_affine_point(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_points_tensor, uint64_t result_points_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO);
    set_named_input(pipeline_handle, op, affine_tensor, "affine");
    set_named_input(pipeline_handle, op, src_points_tensor, "src points");
    set_named_output(pipeline_handle, op, result_points_tensor, "dst points");
}

void OpenXRPicoSecureMR::op_uv_to_3d(uint64_t pipeline_handle, uint64_t uv_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t result_points3d_tensor) {
    uint64_t op = wrapper->create_operator_uv_to_3d(pipeline_handle);
    set_named_input(pipeline_handle, op, uv_tensor, "uv");
    set_named_input(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_input(pipeline_handle, op, camera_matrix_tensor, "camera intrinsic");
    set_named_input(pipeline_handle, op, left_image_tensor, "left image");
    set_named_input(pipeline_handle, op, right_image_tensor, "right image");
    set_named_output(pipeline_handle, op, result_points3d_tensor, "point_xyz");
}

void OpenXRPicoSecureMR::op_argmax(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t result_indices_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO);
    set_named_input(pipeline_handle, op, src_tensor, "operand");
    set_named_output(pipeline_handle, op, result_indices_tensor, "result");
}

void OpenXRPicoSecureMR::op_sort_vec(uint64_t pipeline_handle, uint64_t src_vec_tensor, uint64_t result_sorted_vec_tensor, uint64_t result_indices_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO);
    set_named_input(pipeline_handle, op, src_vec_tensor, "input");
    set_named_output(pipeline_handle, op, result_sorted_vec_tensor, "sorted");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_nms(uint64_t pipeline_handle, uint64_t scores_tensor, uint64_t boxes_tensor, uint64_t result_scores_tensor, uint64_t result_boxes_tensor, uint64_t result_indices_tensor, float threshold) {
    uint64_t op = wrapper->create_operator_nms(pipeline_handle, threshold);
    set_named_input(pipeline_handle, op, scores_tensor, "scores");
    set_named_input(pipeline_handle, op, boxes_tensor, "boxes");
    set_named_output(pipeline_handle, op, result_scores_tensor, "scores");
    set_named_output(pipeline_handle, op, result_boxes_tensor, "boxes");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_compare(uint64_t pipeline_handle, int32_t comparison, uint64_t left_tensor, uint64_t right_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_comparison(pipeline_handle, comparison);
    set_named_input(pipeline_handle, op, left_tensor, "operand0");
    set_named_input(pipeline_handle, op, right_tensor, "operand1");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_sort_mat(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_sorted_mat_tensor, uint64_t result_indices_tensor, int32_t sort_type) {
    uint64_t op = wrapper->create_operator_sort_matrix(pipeline_handle, sort_type);
    set_named_input(pipeline_handle, op, src_mat_tensor, "input");
    set_named_output(pipeline_handle, op, result_sorted_mat_tensor, "sorted");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_render_text(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t text_tensor, uint64_t start_position_tensor, uint64_t colors_tensor, uint64_t texture_id_tensor, uint64_t font_size_tensor, int32_t typeface, const String &language_and_locale, int32_t width, int32_t height) {
    uint64_t op = wrapper->create_operator_render_text(pipeline_handle, typeface, language_and_locale, width, height);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, text_tensor, "text");
    set_named_input(pipeline_handle, op, start_position_tensor, "start");
    set_named_input(pipeline_handle, op, colors_tensor, "colors");
    set_named_input(pipeline_handle, op, texture_id_tensor, "texture ID");
    set_named_input(pipeline_handle, op, font_size_tensor, "font size");
}

// Note: Some operators used in utils (e.g., SVD, NORM, HWC<->CHW) may not be available in this header set.

void OpenXRPicoSecureMR::op_inversion(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_inverted_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO);
    set_named_input(pipeline_handle, op, src_mat_tensor, "operand");
    set_named_output(pipeline_handle, op, result_inverted_tensor, "result");
}

void OpenXRPicoSecureMR::op_transform(uint64_t pipeline_handle, uint64_t rotation_tensor, uint64_t translation_tensor, uint64_t scale_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO);
    set_named_input(pipeline_handle, op, rotation_tensor, "rotation");
    set_named_input(pipeline_handle, op, translation_tensor, "translation");
    if (scale_tensor != 0) {
        set_named_input(pipeline_handle, op, scale_tensor, "scale");
    }
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_gltf_new_texture(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t image_tensor, uint64_t texture_id_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, image_tensor, "rgb image");
    set_named_output(pipeline_handle, op, texture_id_tensor, "texture ID");
}

void OpenXRPicoSecureMR::op_gltf_switch_render(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t pose_tensor, uint64_t view_locked_tensor, uint64_t visible_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, pose_tensor, "world pose");
    set_named_input(pipeline_handle, op, view_locked_tensor, "view locked");
    set_named_input(pipeline_handle, op, visible_tensor, "visible");
}

void OpenXRPicoSecureMR::op_gltf_update(uint64_t pipeline_handle, int32_t attribute, uint64_t gltf_placeholder_tensor, const Dictionary &operands_by_name) {
    // Build the update operator with attribute header on wrapper
    uint64_t op = wrapper->create_operator_update_gltf(pipeline_handle, attribute);
    // Always set gltf first
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");

    // Apply provided operands by their required names
    Array keys = operands_by_name.keys();
    for (int i = 0; i < keys.size(); i++) {
        String name = keys[i];
        uint64_t tensor = (uint64_t)operands_by_name[name];
        if (tensor != 0) {
            wrapper->set_operator_input_by_name(pipeline_handle, op, tensor, name);
        }
    }
}

// Minimal string->enum mapping for common SecureMR operators used by the MNIST sample.
static int _oxr_securemr_op_from_string(const String &s) {
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UNKNOWN_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UNKNOWN_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO;
    return -1;
}

static int32_t _oxr_securemr_encoding_from_data_type(int32_t data_type) {
    switch (data_type) {
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_UFIXED_POINT8_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT8_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_SFIXED_POINT8_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT16_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_UFIXED_POINT16_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_INT32_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO:
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT64_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
        default:
            return XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
    }
}

Dictionary OpenXRPicoSecureMR::deserialize_pipeline(uint64_t framework_handle, const Dictionary &spec, const String &assets_base_path) {
    Dictionary out;
    ERR_FAIL_NULL_V(wrapper, out);

    uint64_t pipeline = create_pipeline(framework_handle);
    out["pipeline"] = (uint64_t)pipeline;
    _release_pipeline_buffers(pipeline);

    Dictionary tensors_out;
    Dictionary tensor_data_types;

    // Tensors
    if (spec.has("tensors") && spec["tensors"].get_type() == Variant::DICTIONARY) {
        Dictionary tensors = spec["tensors"];
        Array tnames = tensors.keys();
        for (int i = 0; i < tnames.size(); i++) {
            String tname = tnames[i];
            Variant tv = tensors[tname];
            if (tv.get_type() != Variant::DICTIONARY) continue;
            Dictionary td = (Dictionary)tv;

            // Dimensions
            PackedInt32Array dims;
            if (td.has("dimensions") && td["dimensions"].get_type() == Variant::ARRAY) {
                Array da = td["dimensions"];
                dims.resize(da.size());
                for (int di = 0; di < da.size(); di++) dims.set(di, (int32_t)(int64_t)da[di]);
            }

            int32_t channels = td.has("channels") ? (int32_t)(int64_t)td["channels"] : 1;
            int32_t data_type = td.has("data_type") ? (int32_t)(int64_t)td["data_type"] : 6; // float32 default
            int32_t tensor_type = td.has("usage") ? (int32_t)(int64_t)td["usage"] : 6;    // mat default
            bool placeholder = td.has("is_placeholder") ? (bool)td["is_placeholder"] : false;

            uint64_t ph = create_pipeline_tensor_shape(pipeline, dims, data_type, channels, tensor_type, placeholder);
            tensors_out[tname] = (uint64_t)ph;
            tensor_data_types[tname] = data_type;

            // Optional initial value
            if (td.has("value") && td["value"].get_type() == Variant::ARRAY) {
                Array arr = td["value"];
                if (data_type == 6) {
                    PackedFloat32Array f;
                    f.resize(arr.size());
                    for (int vi = 0; vi < arr.size(); vi++) f.set(vi, (float)(double)arr[vi]);
                    reset_pipeline_tensor_floats(pipeline, ph, f);
                } else {
                    PackedByteArray b;
                    b.resize(arr.size());
                    for (int vi = 0; vi < arr.size(); vi++) b.set(vi, (uint8_t)(int64_t)arr[vi]);
                    reset_pipeline_tensor_bytes(pipeline, ph, b);
                }
            }
        }
    }

    // Operators
    if (spec.has("operators") && spec["operators"].get_type() == Variant::ARRAY) {
        Array ops = spec["operators"];
        for (int oi = 0; oi < ops.size(); oi++) {
            if (ops[oi].get_type() != Variant::DICTIONARY) continue;
            Dictionary od = ops[oi];
            String type_str = od.has("type") ? (String)od["type"] : String();
            int type = _oxr_securemr_op_from_string(type_str);
            uint64_t oph = 0;

            if (type == XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO) {
                String expr = od.has("expression") ? (String)od["expression"] : String();
                oph = wrapper->create_operator_arithmetic_compose(pipeline, expr);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO) {
                int32_t flag = od.has("flag") ? (int32_t)(int64_t)od["flag"] : 0;
                oph = wrapper->create_operator_convert_color(pipeline, flag);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO) {
                int32_t normalize_type = od.has("normalize_type") ? (int32_t)(int64_t)od["normalize_type"] : 0;
                oph = wrapper->create_operator_normalize(pipeline, normalize_type);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO) {
                String model_asset = od.has("model_asset") ? (String)od["model_asset"] : String();
                String model_name = od.has("model_name") ? (String)od["model_name"] : String("model");

                String resolved_model_path = model_asset;
                PackedByteArray model_data;
                if (model_asset.length() > 0) {
                    String path = model_asset;
                    if (assets_base_path.length() > 0 && !model_asset.begins_with("res://") && !model_asset.begins_with("user://")) {
                        path = assets_base_path.path_join(model_asset);
                    }
                    resolved_model_path = path;
                    if (FileAccess::file_exists(path)) {
                        model_data = FileAccess::get_file_as_bytes(path);
                    } else {
                        UtilityFunctions::push_error(vformat("[PicoSecureMR] Model asset '%s' not found.", path));
                    }
                }
                if (model_data.is_empty()) {
                    UtilityFunctions::push_error(vformat("[PicoSecureMR] Model asset '%s' could not be loaded or is empty.", resolved_model_path));
                    continue;
                }

                // Persist the model buffer so native runtime can read it after deserialization returns.
                PackedByteArray stored_model = _retain_pipeline_buffer(pipeline, model_data);
                if (stored_model.is_empty()) {
                    UtilityFunctions::push_error(vformat("[PicoSecureMR] Failed to retain model buffer for pipeline %llu.", (uint64_t)pipeline));
                    continue;
                }

                String input_name = "input";
                if (od.has("inputs") && od["inputs"].get_type() == Variant::ARRAY) {
                    Array ia = od["inputs"];
                    if (ia.size() > 0 && ia[0].get_type() == Variant::DICTIONARY) {
                        Dictionary iid = ia[0];
                        if (iid.has("name")) input_name = (String)iid["name"];
                    }
                }
                PackedStringArray out_names;
                PackedInt32Array out_enc;
                if (od.has("outputs") && od["outputs"].get_type() == Variant::ARRAY) {
                    Array oa = od["outputs"];
                    for (int i = 0; i < oa.size(); i++) {
                        if (oa[i].get_type() != Variant::DICTIONARY) {
                            continue;
                        }
                        Dictionary ood = oa[i];
                        if (!ood.has("name")) {
                            continue;
                        }
                        String output_name = (String)ood["name"];
                        out_names.push_back(output_name);

                        int32_t encoding = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
                        if (ood.has("encoding")) {
                            encoding = (int32_t)(int64_t)ood["encoding"];
                        } else if (ood.has("tensor")) {
                            String tensor_name = (String)ood["tensor"];
                            if (tensor_data_types.has(tensor_name)) {
                                encoding = _oxr_securemr_encoding_from_data_type((int32_t)(int64_t)tensor_data_types[tensor_name]);
                            }
                        }
                        int new_index = out_enc.size();
                        out_enc.resize(new_index + 1);
                        out_enc.set(new_index, encoding);
                    }
                }

                oph = wrapper->create_operator_model(pipeline, stored_model, model_name, input_name, out_names, out_enc);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO) {
                float threshold = od.has("threshold") ? (float)(double)od["threshold"] : 0.5f;
                oph = wrapper->create_operator_nms(pipeline, threshold);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO) {
                int32_t cmp = od.has("comparison") ? (int32_t)(int64_t)od["comparison"] : 0;
                oph = wrapper->create_operator_comparison(pipeline, cmp);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO) {
                int32_t sort_type = od.has("sort_type") ? (int32_t)(int64_t)od["sort_type"] : 0;
                oph = wrapper->create_operator_sort_matrix(pipeline, sort_type);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO) {
                int32_t typeface = od.has("typeface") ? (int32_t)(int64_t)od["typeface"] : 0;
                String lang = od.has("language_and_locale") ? (String)od["language_and_locale"] : (od.has("language") ? (String)od["language"] : String("en-US"));
                int32_t width = od.has("width") ? (int32_t)(int64_t)od["width"] : 256;
                int32_t height = od.has("height") ? (int32_t)(int64_t)od["height"] : 256;
                oph = wrapper->create_operator_render_text(pipeline, typeface, lang, width, height);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO) {
                int32_t attribute = od.has("attribute") ? (int32_t)(int64_t)od["attribute"] : 0;
                oph = wrapper->create_operator_update_gltf(pipeline, attribute);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO) {
                oph = wrapper->create_operator_uv_to_3d(pipeline);
            } else {
                if (type < 0) {
                    UtilityFunctions::push_error(String("Unknown SecureMR operator type: ") + type_str);
                    continue;
                }
                oph = wrapper->create_operator_basic(pipeline, type);
            }

            // Inputs wiring
            if (od.has("inputs") && od["inputs"].get_type() == Variant::ARRAY) {
                Array ia = od["inputs"];
                for (int i = 0; i < ia.size(); i++) {
                    if (ia[i].get_type() == Variant::STRING) {
                        String tname = ia[i];
                        if (tensors_out.has(tname)) wrapper->set_operator_input_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                    } else if (ia[i].get_type() == Variant::DICTIONARY) {
                        Dictionary id = ia[i];
                        String tname = id.has("tensor") ? (String)id["tensor"] : String();
                        String in_name = id.has("name") ? (String)id["name"] : String();
                        if (tname.length() > 0 && tensors_out.has(tname)) {
                            if (in_name.length() > 0) wrapper->set_operator_input_by_name(pipeline, oph, (uint64_t)tensors_out[tname], in_name);
                            else wrapper->set_operator_input_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                        }
                    }
                }
            }

            // Outputs wiring
            if (od.has("outputs") && od["outputs"].get_type() == Variant::ARRAY) {
                Array oa = od["outputs"];
                for (int i = 0; i < oa.size(); i++) {
                    if (oa[i].get_type() == Variant::STRING) {
                        String tname = oa[i];
                        if (tensors_out.has(tname)) wrapper->set_operator_output_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                    } else if (oa[i].get_type() == Variant::DICTIONARY) {
                        Dictionary od2 = oa[i];
                        String tname = od2.has("tensor") ? (String)od2["tensor"] : String();
                        String out_name = od2.has("name") ? (String)od2["name"] : String();
                        if (tname.length() > 0 && tensors_out.has(tname)) {
                            if (out_name.length() > 0) wrapper->set_operator_output_by_name(pipeline, oph, (uint64_t)tensors_out[tname], out_name);
                            else wrapper->set_operator_output_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                        }
                    }
                }
            }
        }
    }

    out["tensors"] = tensors_out;
    if (spec.has("inputs")) out["inputs"] = spec["inputs"];
    if (spec.has("outputs")) out["outputs"] = spec["outputs"];
    return out;
}

PackedByteArray OpenXRPicoSecureMR::_retain_pipeline_buffer(uint64_t pipeline_handle, const PackedByteArray &buffer) {
    PackedByteArray out;
    if (pipeline_handle == 0 || buffer.is_empty()) {
        return out;
    }

    Variant key = (uint64_t)pipeline_handle;
    Variant existing = pipeline_model_buffers.get(key, Variant());
    Array stored;
    if (existing.get_type() == Variant::ARRAY) {
        stored = existing;
    }
    // Append a copy of the buffer so the underlying data stays alive while the pipeline exists.
    stored.append(buffer);
    pipeline_model_buffers.set(key, stored);

    Variant last = stored[stored.size() - 1];
    if (last.get_type() == Variant::PACKED_BYTE_ARRAY) {
        out = last;
    }
    return out;
}

void OpenXRPicoSecureMR::_release_pipeline_buffers(uint64_t pipeline_handle) {
    if (pipeline_handle == 0 || pipeline_model_buffers.is_empty()) {
        return;
    }

    Variant key = (uint64_t)pipeline_handle;
    pipeline_model_buffers.erase(key);
}

void OpenXRPicoSecureMR::_stop_all_tensor_readbacks() {
    std::vector<std::shared_ptr<TensorReadbackWorker>> workers;
    {
        std::lock_guard<std::mutex> lock(readback_workers_mutex);
        workers.reserve(readback_workers.size());
        for (auto &entry : readback_workers) {
            workers.push_back(entry.second);
        }
        readback_workers.clear();
    }

    for (auto &worker : workers) {
        if (worker) {
            worker->stop();
        }
    }
}

void OpenXRPicoSecureMR::_bind_methods() {
    // Static singleton accessor for GDScript (ClassName.get_singleton()).
    ClassDB::bind_static_method("OpenXRPicoSecureMR", D_METHOD("get_singleton"), &OpenXRPicoSecureMR::get_singleton);

    ClassDB::bind_method(D_METHOD("is_supported"), &OpenXRPicoSecureMR::is_supported);

    ClassDB::bind_method(D_METHOD("create_framework", "image_width", "image_height"), &OpenXRPicoSecureMR::create_framework);
    ClassDB::bind_method(D_METHOD("destroy_framework", "framework_handle"), &OpenXRPicoSecureMR::destroy_framework);
    ClassDB::bind_method(D_METHOD("create_pipeline", "framework_handle"), &OpenXRPicoSecureMR::create_pipeline);
    ClassDB::bind_method(D_METHOD("destroy_pipeline", "pipeline_handle"), &OpenXRPicoSecureMR::destroy_pipeline);

    ClassDB::bind_method(D_METHOD("create_pipeline_tensor_shape", "pipeline_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMR::create_pipeline_tensor_shape);
    ClassDB::bind_method(D_METHOD("create_global_tensor_shape", "framework_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMR::create_global_tensor_shape);
    ClassDB::bind_method(D_METHOD("create_pipeline_tensor_gltf", "pipeline_handle", "buffer", "placeholder"), &OpenXRPicoSecureMR::create_pipeline_tensor_gltf);
    ClassDB::bind_method(D_METHOD("create_global_tensor_gltf", "framework_handle", "buffer", "placeholder"), &OpenXRPicoSecureMR::create_global_tensor_gltf);

    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_bytes", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMR::reset_pipeline_tensor_bytes);
    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_floats", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMR::reset_pipeline_tensor_floats);


    ClassDB::bind_method(D_METHOD("create_operator_basic", "pipeline_handle", "operator_type"), &OpenXRPicoSecureMR::create_operator_basic);
    ClassDB::bind_method(D_METHOD("create_operator_arithmetic", "pipeline_handle", "config_text"), &OpenXRPicoSecureMR::create_operator_arithmetic);
    ClassDB::bind_method(D_METHOD("create_operator_convert_color", "pipeline_handle", "convert_code"), &OpenXRPicoSecureMR::create_operator_convert_color);
    ClassDB::bind_method(D_METHOD("create_operator_normalize", "pipeline_handle", "normalize_type"), &OpenXRPicoSecureMR::create_operator_normalize);
    ClassDB::bind_method(D_METHOD("create_operator_model", "pipeline_handle", "model_data", "model_name", "input_name", "output_names", "output_encodings"), &OpenXRPicoSecureMR::create_operator_model);

    ClassDB::bind_method(D_METHOD("set_operator_input_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMR::set_operator_input_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMR::set_operator_output_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_input_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMR::set_operator_input_by_index);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMR::set_operator_output_by_index);

    ClassDB::bind_method(D_METHOD("execute_pipeline", "pipeline_handle", "mappings"), &OpenXRPicoSecureMR::execute_pipeline);
    ClassDB::bind_method(D_METHOD("start_tensor_readback", "targets", "polling_interval_ms"), &OpenXRPicoSecureMR::start_tensor_readback, DEFVAL(33));
    ClassDB::bind_method(D_METHOD("stop_tensor_readback", "readback_handle"), &OpenXRPicoSecureMR::stop_tensor_readback);
    ClassDB::bind_method(D_METHOD("poll_tensor_readback", "readback_handle"), &OpenXRPicoSecureMR::poll_tensor_readback);

    // Convenience ops
    ClassDB::bind_method(D_METHOD("op_camera_access", "pipeline_handle", "left_image_tensor", "right_image_tensor", "timestamp_tensor", "camera_matrix_tensor"), &OpenXRPicoSecureMR::op_camera_access);
    ClassDB::bind_method(D_METHOD("op_camera_space_to_world", "pipeline_handle", "timestamp_tensor", "left_transform_tensor", "right_transform_tensor"), &OpenXRPicoSecureMR::op_camera_space_to_world);
    ClassDB::bind_method(D_METHOD("op_assignment", "pipeline_handle", "src_tensor", "dst_tensor"), &OpenXRPicoSecureMR::op_assignment);
    ClassDB::bind_method(D_METHOD("op_arithmetic_compose", "pipeline_handle", "expression", "operand_tensors", "result_tensor"), &OpenXRPicoSecureMR::op_arithmetic_compose);
    ClassDB::bind_method(D_METHOD("op_elementwise_min", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_min);
    ClassDB::bind_method(D_METHOD("op_elementwise_max", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_max);
    ClassDB::bind_method(D_METHOD("op_elementwise_multiply", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_multiply);
    ClassDB::bind_method(D_METHOD("op_elementwise_or", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_or);
    ClassDB::bind_method(D_METHOD("op_elementwise_and", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_and);
    ClassDB::bind_method(D_METHOD("op_all", "pipeline_handle", "operand_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_all);
    ClassDB::bind_method(D_METHOD("op_any", "pipeline_handle", "operand_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_any);
    ClassDB::bind_method(D_METHOD("op_solve_pnp", "pipeline_handle", "object_points_tensor", "image_points_tensor", "camera_matrix_tensor", "rot_result_tensor", "trans_result_tensor"), &OpenXRPicoSecureMR::op_solve_pnp);
    ClassDB::bind_method(D_METHOD("op_get_affine", "pipeline_handle", "src_points_tensor", "dst_points_tensor", "result_affine_tensor"), &OpenXRPicoSecureMR::op_get_affine);
    ClassDB::bind_method(D_METHOD("op_apply_affine", "pipeline_handle", "affine_tensor", "src_image_tensor", "result_image_tensor"), &OpenXRPicoSecureMR::op_apply_affine);
    ClassDB::bind_method(D_METHOD("op_apply_affine_point", "pipeline_handle", "affine_tensor", "src_points_tensor", "result_points_tensor"), &OpenXRPicoSecureMR::op_apply_affine_point);
    ClassDB::bind_method(D_METHOD("op_uv_to_3d", "pipeline_handle", "uv_tensor", "timestamp_tensor", "camera_matrix_tensor", "left_image_tensor", "right_image_tensor", "result_points3d_tensor"), &OpenXRPicoSecureMR::op_uv_to_3d);
    ClassDB::bind_method(D_METHOD("op_argmax", "pipeline_handle", "src_tensor", "result_indices_tensor"), &OpenXRPicoSecureMR::op_argmax);
    ClassDB::bind_method(D_METHOD("op_sort_vec", "pipeline_handle", "src_vec_tensor", "result_sorted_vec_tensor", "result_indices_tensor"), &OpenXRPicoSecureMR::op_sort_vec);
    ClassDB::bind_method(D_METHOD("op_nms", "pipeline_handle", "scores_tensor", "boxes_tensor", "result_scores_tensor", "result_boxes_tensor", "result_indices_tensor", "threshold"), &OpenXRPicoSecureMR::op_nms);
    ClassDB::bind_method(D_METHOD("op_compare", "pipeline_handle", "comparison", "left_tensor", "right_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_compare);
    ClassDB::bind_method(D_METHOD("op_sort_mat", "pipeline_handle", "src_mat_tensor", "result_sorted_mat_tensor", "result_indices_tensor", "sort_type"), &OpenXRPicoSecureMR::op_sort_mat);
    ClassDB::bind_method(D_METHOD("op_render_text", "pipeline_handle", "gltf_placeholder_tensor", "text_tensor", "start_position_tensor", "colors_tensor", "texture_id_tensor", "font_size_tensor", "typeface", "language_and_locale", "width", "height"), &OpenXRPicoSecureMR::op_render_text);
    ClassDB::bind_method(D_METHOD("op_inversion", "pipeline_handle", "src_mat_tensor", "result_inverted_tensor"), &OpenXRPicoSecureMR::op_inversion);
    ClassDB::bind_method(D_METHOD("op_transform", "pipeline_handle", "rotation_tensor", "translation_tensor", "scale_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_transform);
    ClassDB::bind_method(D_METHOD("op_gltf_new_texture", "pipeline_handle", "gltf_placeholder_tensor", "image_tensor", "texture_id_tensor"), &OpenXRPicoSecureMR::op_gltf_new_texture);
    ClassDB::bind_method(D_METHOD("op_gltf_switch_render", "pipeline_handle", "gltf_placeholder_tensor", "pose_tensor", "view_locked_tensor", "visible_tensor"), &OpenXRPicoSecureMR::op_gltf_switch_render);
    ClassDB::bind_method(D_METHOD("op_gltf_update", "pipeline_handle", "attribute", "gltf_placeholder_tensor", "operands_by_name"), &OpenXRPicoSecureMR::op_gltf_update);

    // Deserialization
    ClassDB::bind_method(D_METHOD("deserialize_pipeline", "framework_handle", "spec", "assets_base_path"), &OpenXRPicoSecureMR::deserialize_pipeline, DEFVAL(String("")));
}

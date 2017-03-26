/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "overlaybuffermanager.h"

#include "hwctrace.h"
#include "overlaylayer.h"

namespace hwcomposer {

ImportedBuffer::~ImportedBuffer() {
  if (buffer_)
    buffer_manager_->UnRegisterBuffer(buffer_);
}

OverlayBufferManager::~OverlayBufferManager() {
}

bool OverlayBufferManager::Initialize(uint32_t gpu_fd) {
  buffer_handler_.reset(NativeBufferHandler::CreateInstance(gpu_fd));
  if (!buffer_handler_) {
    ETRACE("Failed to create native buffer handler instance");
    return false;
  }

  return true;
}

ImportedBuffer* OverlayBufferManager::CreateBuffer(const HwcBuffer& bo) {
  ScopedSpinLock lock(spin_lock_);
  buffers_.emplace_back();
  Buffer& buffer = buffers_.back();
  buffer.buffer_.reset(new OverlayBuffer());
  buffer.buffer_->Initialize(bo);
  buffer.ref_count_ = 1;
  buffer.sync_object_.reset(new NativeSync());
  if (!buffer.sync_object_->Init()) {
    ETRACE("Failed to create sync object.");
  }

  return new ImportedBuffer(buffer.buffer_.get(), this,
                            buffer.sync_object_->CreateNextTimelineFence());
}

ImportedBuffer* OverlayBufferManager::CreateBufferFromNativeHandle(
    HWCNativeHandle handle) {
  ScopedSpinLock lock(spin_lock_);
  buffers_.emplace_back();
  Buffer& buffer = buffers_.back();
  buffer.buffer_.reset(new OverlayBuffer());
  buffer.buffer_->InitializeFromNativeHandle(handle, buffer_handler_.get());
  buffer.ref_count_ = 1;
  buffer.sync_object_.reset(new NativeSync());
  if (!buffer.sync_object_->Init()) {
    ETRACE("Failed to create sync object.");
  }

  return new ImportedBuffer(buffer.buffer_.get(), this,
                            buffer.sync_object_->CreateNextTimelineFence());
}

void OverlayBufferManager::RegisterBuffer(OverlayBuffer* buffer) {
  ScopedSpinLock lock(spin_lock_);
  for (Buffer& overlay_buffer : buffers_) {
    if (overlay_buffer.buffer_.get() != buffer)
      continue;

    overlay_buffer.ref_count_++;
    break;
  }
}

void OverlayBufferManager::RegisterBuffers(
    const std::vector<OverlayBuffer*>& buffers) {
  ScopedSpinLock lock(spin_lock_);
  for (OverlayBuffer* buffer : buffers) {
    for (Buffer& overlay_buffer : buffers_) {
      if (overlay_buffer.buffer_.get() != buffer)
        continue;

      overlay_buffer.ref_count_++;
      break;
    }
  }
}

void OverlayBufferManager::UnRegisterBuffer(OverlayBuffer* buffer) {
  ScopedSpinLock lock(spin_lock_);
  int32_t index = -1;
  for (Buffer& overlay_buffer : buffers_) {
    index++;
    if (overlay_buffer.buffer_.get() != buffer)
      continue;

    overlay_buffer.ref_count_--;
    if (overlay_buffer.ref_count_ <= 0) {
      buffers_.erase(buffers_.begin() + index);
    }

    break;
  }
}

void OverlayBufferManager::UnRegisterBuffers(
    const std::vector<OverlayBuffer*>& buffers) {
  ScopedSpinLock lock(spin_lock_);
  for (OverlayBuffer* buffer : buffers) {
    int32_t index = -1;
    for (Buffer& overlay_buffer : buffers_) {
      index++;
      if (overlay_buffer.buffer_.get() != buffer)
        continue;

      overlay_buffer.ref_count_--;
      if (overlay_buffer.ref_count_ <= 0) {
        buffers_.erase(buffers_.begin() + index);
      }

      break;
    }
  }
}

void OverlayBufferManager::SignalBuffersIfReady(
    std::vector<OverlayLayer>& layers) {
  ScopedSpinLock lock(spin_lock_);
  for (OverlayLayer& layer : layers) {
    if (layer.GetReleaseFenceState() != NativeSync::State::kReady) {
      continue;
    }

    OverlayBuffer* buffer = layer.GetBuffer();
    if (!buffer)
      continue;

    for (Buffer& overlay_buffer : buffers_) {
      if (overlay_buffer.buffer_.get() != buffer)
        continue;

      overlay_buffer.sync_object_.reset(nullptr);
      break;
    }
  }
}

void OverlayBufferManager::UnRegisterLayerBuffers(
    std::vector<OverlayLayer>& layers) {
  ScopedSpinLock lock(spin_lock_);
  for (OverlayLayer& layer : layers) {
    OverlayBuffer* buffer = layer.GetBuffer();
    if (!buffer)
      continue;
    int32_t index = -1;
    for (Buffer& overlay_buffer : buffers_) {
      index++;
      if (overlay_buffer.buffer_.get() != buffer)
        continue;

      overlay_buffer.ref_count_--;
      layer.MarkBufferReleased();
      if (overlay_buffer.ref_count_ <= 0) {
        buffers_.erase(buffers_.begin() + index);
      }

      break;
    }
  }
}

}  // namespace hwcomposer
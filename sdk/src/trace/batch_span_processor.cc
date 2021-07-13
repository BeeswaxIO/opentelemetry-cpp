// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/trace/batch_span_processor.h"

#include <vector>
#include <iostream>
using opentelemetry::sdk::common::AtomicUniquePtr;
using opentelemetry::sdk::common::CircularBuffer;
using opentelemetry::sdk::common::CircularBufferRange;
using opentelemetry::trace::SpanContext;

namespace {
  template <typename Id>
  std::string IdString(const Id& id) {
    constexpr auto num_bytes = Id::kSize;
    constexpr auto num_chars = 2 * num_bytes;

    std::string result(num_chars, '0');
    opentelemetry::nostd::span<char, num_chars> result_span(&(*result.begin()),
							    num_chars);

    id.ToLowerBase16(result_span);
    return result;
  }
}

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
BatchSpanProcessor::BatchSpanProcessor(std::unique_ptr<SpanExporter> &&exporter,
                                       const BatchSpanProcessorOptions &options)
    : exporter_(std::move(exporter)),
      max_queue_size_(options.max_queue_size),
      schedule_delay_millis_(options.schedule_delay_millis),
      max_export_batch_size_(options.max_export_batch_size),
      buffer_(max_queue_size_),
      worker_thread_(&BatchSpanProcessor::DoBackgroundWork, this)
{}

std::unique_ptr<Recordable> BatchSpanProcessor::MakeRecordable() noexcept
{
  return exporter_->MakeRecordable();
}

void BatchSpanProcessor::OnStart(Recordable& recordable, const SpanContext & parent_context) noexcept
{
  std::cout << "bjlbjl BatchSpanProcessor::OnStart no-op OnStart "
	    << IdString(parent_context.trace_id()) << " " << IdString(parent_context.span_id())
	    << " " << &recordable
	    << std::endl;
  std::cout << "bjlbjl BatchSpanProcessor::OnStart no-op recordable print "
	    << &recordable
	    << std::endl;
  recordable.Print();
}

void BatchSpanProcessor::OnEnd(std::unique_ptr<Recordable> &&span) noexcept
{
  std::cout << "bjlbjl BatchSpanProcessor::OnEnd printing " << span.get()
	    << std::endl;
  if (span)
    span->Print();

  if (is_shutdown_.load() == true)
  {
    std::cout << "bjlbjl BatchSpanProcessor::OnEnd shutdown load "
	      << span.get()
	      << std::endl;
    return;
  }

  if (buffer_.Add(span) == false)
  {
    std::cout << "bjlbjl BatchSpanProcessor::OnEnd add span false "
	      << span.get()
	      << std::endl;
    return;
  }

  std::cout << "bjlbjl BatchSpanProcessor::OnEnd possibly notify "
	    << span.get()
	    << std::endl;

  // If the queue gets at least half full a preemptive notification is
  // sent to the worker thread to start a new export cycle.
  if (buffer_.size() >= max_queue_size_ / 2)
  {
    // signal the worker thread
    std::cout << "bjlbjl BatchSpanProcessor::OnEnd notify "
	      << span.get()
	      << std::endl;
    cv_.notify_one();
  }
}

bool BatchSpanProcessor::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  std::cout << "bjlbjl BatchSpanProcessor::ForceFlush called" << std::endl;
  
  if (is_shutdown_.load() == true)
  {
    return false;
  }

  is_force_flush_ = true;

  // Keep attempting to wake up the worker thread
  while (is_force_flush_.load() == true)
  {
    cv_.notify_one();
  }

  // Now wait for the worker thread to signal back from the Export method
  std::unique_lock<std::mutex> lk(force_flush_cv_m_);
  while (is_force_flush_notified_.load() == false)
  {
    force_flush_cv_.wait(lk);
  }

  // Notify the worker thread
  is_force_flush_notified_ = false;

  return true;
}

void BatchSpanProcessor::DoBackgroundWork()
{
  auto timeout = schedule_delay_millis_;

  while (true)
  {
    // Wait for `timeout` milliseconds
    std::unique_lock<std::mutex> lk(cv_m_);
    cv_.wait_for(lk, timeout);

    if (is_shutdown_.load() == true)
    {
      DrainQueue();
      return;
    }

    bool was_force_flush_called = is_force_flush_.load();

    // Check if this export was the result of a force flush.
    if (was_force_flush_called == true)
    {
      // Since this export was the result of a force flush, signal the
      // main thread that the worker thread has been notified
      is_force_flush_ = false;
    }
    else
    {
      // If the buffer was empty during the entire `timeout` time interval,
      // go back to waiting. If this was a spurious wake-up, we export only if
      // `buffer_` is not empty. This is acceptable because batching is a best
      // mechanism effort here.
      if (buffer_.empty() == true)
      {
        timeout = schedule_delay_millis_;
        continue;
      }
    }

    auto start = std::chrono::steady_clock::now();
    Export(was_force_flush_called);
    auto end      = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Subtract the duration of this export call from the next `timeout`.
    timeout = schedule_delay_millis_ - duration;
  }
}

void BatchSpanProcessor::Export(const bool was_force_flush_called)
{
  std::cout << "bjlbjl BatchSpanProcessor::Export " << was_force_flush_called << std::endl;
  std::vector<std::unique_ptr<Recordable>> spans_arr;

  size_t num_spans_to_export;

  if (was_force_flush_called == true)
  {
    num_spans_to_export = buffer_.size();
  }
  else
  {
    num_spans_to_export =
        buffer_.size() >= max_export_batch_size_ ? max_export_batch_size_ : buffer_.size();
  }

  buffer_.Consume(num_spans_to_export,
                  [&](CircularBufferRange<AtomicUniquePtr<Recordable>> range) noexcept {
                    range.ForEach([&](AtomicUniquePtr<Recordable> &ptr) {
                      std::unique_ptr<Recordable> swap_ptr = std::unique_ptr<Recordable>(nullptr);
                      ptr.Swap(swap_ptr);
                      spans_arr.push_back(std::unique_ptr<Recordable>(swap_ptr.release()));
		      if (auto& bk = spans_arr.back()) {
			  std::cout << "bjlbjl printing from Consume "
				    << bk.get()
				    << std::endl;
			  bk->Print();
		      } else {
			std::cout << "bjlbjl null back in Consume!" << std::endl;
		      }
                      return true;
                    });
                  });

  exporter_->Export(nostd::span<std::unique_ptr<Recordable>>(spans_arr.data(), spans_arr.size()));

  // Notify the main thread in case this export was the result of a force flush.
  if (was_force_flush_called == true)
  {
    is_force_flush_notified_ = true;
    while (is_force_flush_notified_.load() == true)
    {
      force_flush_cv_.notify_one();
    }
  }
}

void BatchSpanProcessor::DrainQueue()
{
  std::cout << "bjlbjl BatchSpanProcessor::DrainQueue" << std::endl;
  while (buffer_.empty() == false)
  {
    Export(false);
  }
}

bool BatchSpanProcessor::Shutdown(std::chrono::microseconds timeout) noexcept
{
  is_shutdown_.store(true);

  cv_.notify_one();
  worker_thread_.join();
  if (exporter_ != nullptr)
  {
    return exporter_->Shutdown();
  }

  return true;
}

BatchSpanProcessor::~BatchSpanProcessor()
{
  if (is_shutdown_.load() == false)
  {
    Shutdown();
  }
}

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

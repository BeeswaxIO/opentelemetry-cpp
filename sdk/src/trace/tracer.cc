// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/common/atomic_shared_ptr.h"
#include "opentelemetry/version.h"
#include "src/trace/span.h"

#include <iostream>
#include <memory>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

Tracer::Tracer(std::shared_ptr<sdk::trace::TracerContext> context,
               std::unique_ptr<InstrumentationLibrary> instrumentation_library) noexcept
    : context_{context}, instrumentation_library_{std::move(instrumentation_library)}
{}

nostd::shared_ptr<trace_api::Span> Tracer::StartSpan(
    nostd::string_view name,
    const opentelemetry::common::KeyValueIterable &attributes,
    const trace_api::SpanContextKeyValueIterable &links,
    const trace_api::StartSpanOptions &options) noexcept
{
  if (options.log_start)
    std::cout << "lib: options.parent.IsValid() " << options.parent.IsValid()
	      << " options.parent.IsSampled() " << options.parent.IsSampled() << std::endl;
  trace_api::SpanContext parent_context =
      options.parent.IsValid() ? options.parent : GetCurrentSpan()->GetContext();

  trace_api::TraceId trace_id;
  trace_api::SpanId span_id = GetIdGenerator().GenerateSpanId();
  bool is_parent_span_valid = false;

  if (parent_context.IsValid())
  {
    trace_id             = parent_context.trace_id();
    is_parent_span_valid = true;
  }
  else
  {
    trace_id = GetIdGenerator().GenerateTraceId();
  }
  if (options.log_start) {
    std::cout << "lib: trace " << trace_id << " span " << span_id << std::endl;
  }

  if (options.log_start)
    std::cout << "lib: is_parent_span_valid " << is_parent_span_valid << std::endl;

  auto sampling_result = context_->GetSampler().ShouldSample(parent_context, trace_id, name,
                                                             options.kind, attributes, links, options.log_start);

  if (sampling_result.decision == Decision::DROP)
  {
    // Don't allocate a no-op span for every DROP decision, but use a static
    // singleton for this case.
    static nostd::shared_ptr<trace_api::Span> noop_span(
        new trace_api::NoopSpan{this->shared_from_this()});

    if (options.log_start)
      std::cout << "lib: Decision::DROP" << std::endl;

    return noop_span;
  }
  else
  {
    if (options.log_start)
      std::cout << "lib: not dropping" << std::endl;

    auto span_context = std::unique_ptr<trace_api::SpanContext>(new trace_api::SpanContext(
        trace_id, span_id, trace_api::TraceFlags{trace_api::TraceFlags::kIsSampled}, false,
        sampling_result.trace_state ? sampling_result.trace_state
                                    : is_parent_span_valid ? parent_context.trace_state()
                                                           : trace_api::TraceState::GetDefault()));

    if (options.log_start) {
      std::cout << "lib: span_context " << span_context->IsValid() << " " << span_context->IsSampled() << std::endl;
    }

    auto span = nostd::shared_ptr<trace_api::Span>{
        new (std::nothrow) Span{this->shared_from_this(), name, attributes, links, options,
                                parent_context, std::move(span_context)}};

    if (options.log_start) {
      std::cout << "lib: span " << span->GetContext().IsValid() << " " << span->GetContext().IsSampled() << std::endl;
    }

    // if the attributes is not nullptr, add attributes to the span.
    if (sampling_result.attributes)
    {
      for (auto &kv : *sampling_result.attributes)
      {
        span->SetAttribute(kv.first, kv.second);
      }
    }

    return span;
  }
}

void Tracer::ForceFlushWithMicroseconds(uint64_t timeout) noexcept
{
  (void)timeout;
}

void Tracer::CloseWithMicroseconds(uint64_t timeout) noexcept
{
  (void)timeout;
}
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

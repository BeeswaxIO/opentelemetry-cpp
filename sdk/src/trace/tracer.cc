// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/common/atomic_shared_ptr.h"
#include "opentelemetry/version.h"
#include "src/trace/span.h"

#include <memory>
#include <iostream>

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
    const trace_api::StartSpanOptions &options,
    std::string* log) noexcept
{
  trace_api::SpanContext parent_context =
      options.parent.IsValid() ? options.parent : GetCurrentSpan()->GetContext();
  if (log) {
    *log += "Tracer::StartSpan: valid options context // ";
    *log += "Tracer::StartSpan: parent_context validity // "
      + std::to_string(parent_context.IsValid()) + " // ";
  }

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

  auto sampling_result = context_->GetSampler().ShouldSample(parent_context, trace_id, name,
                                                             options.kind, attributes, links,
							     log);

  if (sampling_result.decision == Decision::DROP)
  {
    // Don't allocate a no-op span for every DROP decision, but use a static
    // singleton for this case.
    static nostd::shared_ptr<trace_api::Span> noop_span(
        new trace_api::NoopSpan{this->shared_from_this()});

    if (log) {
      *log += "Tracer::StartSpan DROP // ";
    }

    return noop_span;
  }
  else
  {
    if (log) {
      *log += "Tracer::StartSpan SAMPLE // ";
      *log += "Tracer::StartSpan sampling_result.trace_state "
	+ std::to_string(sampling_result.trace_state) + " // ";
    }
    auto span_context = std::unique_ptr<trace_api::SpanContext>(new trace_api::SpanContext(
        trace_id, span_id, trace_api::TraceFlags{trace_api::TraceFlags::kIsSampled}, false,
        sampling_result.trace_state ? sampling_result.trace_state
                                    : is_parent_span_valid ? parent_context.trace_state()
                                                           : trace_api::TraceState::GetDefault()));
    if (log) {
      *log += "Tracer::StartSpan span_context->IsValid() "
	+ std::to_string(span_context->IsValid()) + " // ";
    }

    auto span = nostd::shared_ptr<trace_api::Span>{
        new (std::nothrow) Span{this->shared_from_this(), name, attributes, links, options,
                                parent_context, std::move(span_context)}};

    if (log) {
      *log += "Tracer::StartSpan span->GetContext().IsValid() "
	+ std::to_string(span->GetContext().IsValid()) + " // ";
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

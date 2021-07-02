// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/trace/samplers/parent.h"
#include <iostream>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
ParentBasedSampler::ParentBasedSampler(std::shared_ptr<Sampler> delegate_sampler) noexcept
    : delegate_sampler_(delegate_sampler),
      description_("ParentBased{" + std::string{delegate_sampler->GetDescription()} + "}")
{}

SamplingResult ParentBasedSampler::ShouldSample(
    const trace_api::SpanContext &parent_context,
    trace_api::TraceId trace_id,
    nostd::string_view name,
    trace_api::SpanKind span_kind,
    const opentelemetry::common::KeyValueIterable &attributes,
    const trace_api::SpanContextKeyValueIterable &links,
    std::string* log) noexcept
{
  if (!parent_context.IsValid())
  {
    if (log) {
      *log += "ParentBasedSampler::ShouldSample invalid parent context (delegate decision)\n";
    }
    // If no parent (root span) exists returns the result of the delegateSampler
    return delegate_sampler_->ShouldSample(parent_context, trace_id, name, span_kind, attributes,
                                           links, log);
  }

  if (log) {
    *log += "ParentBasedSampler::ShouldSample valid parent context (not delegating)\n";
  }

  // If parent exists:
  if (parent_context.IsSampled())
  {
    if (log) {
      *log += "ParentBasedSampler::ShouldSample parent_context is sampled\n";
    }
    return {Decision::RECORD_AND_SAMPLE, nullptr, parent_context.trace_state()};
  }

  if (log) {
    *log += "ParentBasedSampler::ShouldSample parent_context is not sampled\n";
  }

  return {Decision::DROP, nullptr, parent_context.trace_state()};
}

nostd::string_view ParentBasedSampler::GetDescription() const noexcept
{
  return description_;
}
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

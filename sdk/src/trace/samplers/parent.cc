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
    bool log) noexcept
{
  if (!parent_context.IsValid())
  {
    if (log)
      std::cout << "lib: PARENT invalid parent context...calling delegate sampler" << std::endl;;

    // If no parent (root span) exists returns the result of the delegateSampler
    return delegate_sampler_->ShouldSample(parent_context, trace_id, name, span_kind, attributes,
						  links, log);
  }
  if (log)
    std::cout << "lib: PARENT valid parent context" << std::endl;

  // If parent exists:
  if (parent_context.IsSampled())
  {
    if (log)
      std::cout << "lib: PARENT parent context is sampled" << std::endl;
    return {Decision::RECORD_AND_SAMPLE, nullptr, parent_context.trace_state()};
  }

  if (log)
    std::cout << "lib: PARENT not sampled, return a drop decision" << std::endl;

  return {Decision::DROP, nullptr, parent_context.trace_state()};
}

nostd::string_view ParentBasedSampler::GetDescription() const noexcept
{
  return description_;
}
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

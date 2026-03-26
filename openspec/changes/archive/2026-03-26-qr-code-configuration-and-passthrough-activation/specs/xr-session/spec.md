## MODIFIED Requirements

### Requirement: Render loop submits composition layers via xrEndFrame
Each frame, the application SHALL call `xrEndFrame` with the clear color alpha set to 0.0f so that the passthrough environment is visible behind rendered content. The `environmentBlendMode` SHALL be set to `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` when available.

#### Scenario: Clear color alpha enables passthrough visibility
- **WHEN** `EndFrame` is called with passthrough active
- **THEN** the clear color alpha is 0.0f and the real-world environment is visible behind virtual content

#### Scenario: Alpha blend mode is used when available
- **WHEN** the runtime supports `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND`
- **THEN** that blend mode is selected for frame submission

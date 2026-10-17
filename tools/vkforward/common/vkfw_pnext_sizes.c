/* GENERATED FILE - do not edit. Regenerate with scripts/gen_pnext_table.py */
/* Source: Vulkan headers VK_HEADER_VERSION 309, LP64 sizes */

#include "vkfw_pnext_sizes.h"

/*
 * Sizes, not sizeof(): this file is compiled against whatever Vulkan
 * headers the toolchain happens to carry, and those move. A struct the
 * local headers renamed or dropped used to break the build of a table
 * that only ever needed the numbers.
 */
static struct { uint32_t sType; uint32_t size; } const kSizes[] = {
    { 0, 48 }, /* ApplicationInfo */
    { 1, 64 }, /* InstanceCreateInfo */
    { 2, 40 }, /* DeviceQueueCreateInfo */
    { 3, 72 }, /* DeviceCreateInfo */
    { 4, 72 }, /* SubmitInfo */
    { 5, 32 }, /* MemoryAllocateInfo */
    { 6, 40 }, /* MappedMemoryRange */
    { 7, 96 }, /* BindSparseInfo */
    { 8, 24 }, /* FenceCreateInfo */
    { 9, 24 }, /* SemaphoreCreateInfo */
    { 10, 24 }, /* EventCreateInfo */
    { 11, 32 }, /* QueryPoolCreateInfo */
    { 12, 56 }, /* BufferCreateInfo */
    { 13, 56 }, /* BufferViewCreateInfo */
    { 14, 88 }, /* ImageCreateInfo */
    { 15, 80 }, /* ImageViewCreateInfo */
    { 16, 40 }, /* ShaderModuleCreateInfo */
    { 17, 40 }, /* PipelineCacheCreateInfo */
    { 18, 48 }, /* PipelineShaderStageCreateInfo */
    { 19, 48 }, /* PipelineVertexInputStateCreateInfo */
    { 20, 32 }, /* PipelineInputAssemblyStateCreateInfo */
    { 21, 24 }, /* PipelineTessellationStateCreateInfo */
    { 22, 48 }, /* PipelineViewportStateCreateInfo */
    { 23, 64 }, /* PipelineRasterizationStateCreateInfo */
    { 24, 48 }, /* PipelineMultisampleStateCreateInfo */
    { 25, 104 }, /* PipelineDepthStencilStateCreateInfo */
    { 26, 56 }, /* PipelineColorBlendStateCreateInfo */
    { 27, 32 }, /* PipelineDynamicStateCreateInfo */
    { 28, 144 }, /* GraphicsPipelineCreateInfo */
    { 29, 96 }, /* ComputePipelineCreateInfo */
    { 30, 48 }, /* PipelineLayoutCreateInfo */
    { 31, 80 }, /* SamplerCreateInfo */
    { 32, 32 }, /* DescriptorSetLayoutCreateInfo */
    { 33, 40 }, /* DescriptorPoolCreateInfo */
    { 34, 40 }, /* DescriptorSetAllocateInfo */
    { 35, 64 }, /* WriteDescriptorSet */
    { 36, 56 }, /* CopyDescriptorSet */
    { 37, 64 }, /* FramebufferCreateInfo */
    { 38, 64 }, /* RenderPassCreateInfo */
    { 39, 24 }, /* CommandPoolCreateInfo */
    { 40, 32 }, /* CommandBufferAllocateInfo */
    { 41, 56 }, /* CommandBufferInheritanceInfo */
    { 42, 32 }, /* CommandBufferBeginInfo */
    { 43, 64 }, /* RenderPassBeginInfo */
    { 44, 56 }, /* BufferMemoryBarrier */
    { 45, 72 }, /* ImageMemoryBarrier */
    { 46, 24 }, /* MemoryBarrier */
    { 49, 64 }, /* PhysicalDeviceVulkan11Features */
    { 50, 112 }, /* PhysicalDeviceVulkan11Properties */
    { 51, 208 }, /* PhysicalDeviceVulkan12Features */
    { 52, 736 }, /* PhysicalDeviceVulkan12Properties */
    { 53, 80 }, /* PhysicalDeviceVulkan13Features */
    { 54, 216 }, /* PhysicalDeviceVulkan13Properties */
    { 55, 104 }, /* PhysicalDeviceVulkan14Features */
    { 56, 144 }, /* PhysicalDeviceVulkan14Properties */
    { 1000001000, 104 }, /* SwapchainCreateInfoKHR */
    { 1000001001, 64 }, /* PresentInfoKHR */
    { 1000002000, 32 }, /* DisplayModeCreateInfoKHR */
    { 1000002001, 64 }, /* DisplaySurfaceCreateInfoKHR */
    { 1000003000, 56 }, /* DisplayPresentInfoKHR */
    { 1000011000, 40 }, /* DebugReportCallbackCreateInfoEXT */
    { 1000018000, 24 }, /* PipelineRasterizationStateRasterizationOrderAMD */
    { 1000022000, 40 }, /* DebugMarkerObjectNameInfoEXT */
    { 1000022001, 56 }, /* DebugMarkerObjectTagInfoEXT */
    { 1000022002, 40 }, /* DebugMarkerMarkerInfoEXT */
    { 1000023000, 32 }, /* VideoProfileInfoKHR */
    { 1000023001, 336 }, /* VideoCapabilitiesKHR */
    { 1000023002, 48 }, /* VideoPictureResourceInfoKHR */
    { 1000023003, 48 }, /* VideoSessionMemoryRequirementsKHR */
    { 1000023004, 48 }, /* BindVideoSessionMemoryInfoKHR */
    { 1000023005, 64 }, /* VideoSessionCreateInfoKHR */
    { 1000023006, 40 }, /* VideoSessionParametersCreateInfoKHR */
    { 1000023007, 24 }, /* VideoSessionParametersUpdateInfoKHR */
    { 1000023008, 56 }, /* VideoBeginCodingInfoKHR */
    { 1000023009, 24 }, /* VideoEndCodingInfoKHR */
    { 1000023010, 24 }, /* VideoCodingControlInfoKHR */
    { 1000023011, 32 }, /* VideoReferenceSlotInfoKHR */
    { 1000023012, 24 }, /* QueueFamilyVideoPropertiesKHR */
    { 1000023013, 32 }, /* VideoProfileListInfoKHR */
    { 1000023014, 24 }, /* PhysicalDeviceVideoFormatInfoKHR */
    { 1000023015, 56 }, /* VideoFormatPropertiesKHR */
    { 1000023016, 24 }, /* QueueFamilyQueryResultStatusPropertiesKHR */
    { 1000024000, 120 }, /* VideoDecodeInfoKHR */
    { 1000024001, 24 }, /* VideoDecodeCapabilitiesKHR */
    { 1000024002, 24 }, /* VideoDecodeUsageInfoKHR */
    { 1000026000, 24 }, /* DedicatedAllocationImageCreateInfoNV */
    { 1000026001, 24 }, /* DedicatedAllocationBufferCreateInfoNV */
    { 1000026002, 32 }, /* DedicatedAllocationMemoryAllocateInfoNV */
    { 1000028000, 24 }, /* PhysicalDeviceTransformFeedbackFeaturesEXT */
    { 1000028001, 64 }, /* PhysicalDeviceTransformFeedbackPropertiesEXT */
    { 1000028002, 24 }, /* PipelineRasterizationStateStreamCreateInfoEXT */
    { 1000029000, 32 }, /* CuModuleCreateInfoNVX */
    { 1000029001, 32 }, /* CuFunctionCreateInfoNVX */
    { 1000029002, 88 }, /* CuLaunchInfoNVX */
    { 1000029004, 24 }, /* CuModuleTexturingModeCreateInfoNVX */
    { 1000030000, 40 }, /* ImageViewHandleInfoNVX */
    { 1000030001, 32 }, /* ImageViewAddressPropertiesNVX */
    { 1000038000, 72 }, /* VideoEncodeH264CapabilitiesKHR */
    { 1000038001, 32 }, /* VideoEncodeH264SessionParametersCreateInfoKHR */
    { 1000038002, 48 }, /* VideoEncodeH264SessionParametersAddInfoKHR */
    { 1000038003, 48 }, /* VideoEncodeH264PictureInfoKHR */
    { 1000038004, 24 }, /* VideoEncodeH264DpbSlotInfoKHR */
    { 1000038005, 32 }, /* VideoEncodeH264NaluSliceInfoKHR */
    { 1000038006, 32 }, /* VideoEncodeH264GopRemainingFrameInfoKHR */
    { 1000038007, 24 }, /* VideoEncodeH264ProfileInfoKHR */
    { 1000038008, 40 }, /* VideoEncodeH264RateControlInfoKHR */
    { 1000038009, 64 }, /* VideoEncodeH264RateControlLayerInfoKHR */
    { 1000038010, 24 }, /* VideoEncodeH264SessionCreateInfoKHR */
    { 1000038011, 64 }, /* VideoEncodeH264QualityLevelPropertiesKHR */
    { 1000038012, 32 }, /* VideoEncodeH264SessionParametersGetInfoKHR */
    { 1000038013, 24 }, /* VideoEncodeH264SessionParametersFeedbackInfoKHR */
    { 1000039000, 88 }, /* VideoEncodeH265CapabilitiesKHR */
    { 1000039001, 40 }, /* VideoEncodeH265SessionParametersCreateInfoKHR */
    { 1000039002, 64 }, /* VideoEncodeH265SessionParametersAddInfoKHR */
    { 1000039003, 40 }, /* VideoEncodeH265PictureInfoKHR */
    { 1000039004, 24 }, /* VideoEncodeH265DpbSlotInfoKHR */
    { 1000039005, 32 }, /* VideoEncodeH265NaluSliceSegmentInfoKHR */
    { 1000039006, 32 }, /* VideoEncodeH265GopRemainingFrameInfoKHR */
    { 1000039007, 24 }, /* VideoEncodeH265ProfileInfoKHR */
    { 1000039009, 40 }, /* VideoEncodeH265RateControlInfoKHR */
    { 1000039010, 64 }, /* VideoEncodeH265RateControlLayerInfoKHR */
    { 1000039011, 24 }, /* VideoEncodeH265SessionCreateInfoKHR */
    { 1000039012, 56 }, /* VideoEncodeH265QualityLevelPropertiesKHR */
    { 1000039013, 40 }, /* VideoEncodeH265SessionParametersGetInfoKHR */
    { 1000039014, 32 }, /* VideoEncodeH265SessionParametersFeedbackInfoKHR */
    { 1000040000, 32 }, /* VideoDecodeH264CapabilitiesKHR */
    { 1000040001, 40 }, /* VideoDecodeH264PictureInfoKHR */
    { 1000040003, 24 }, /* VideoDecodeH264ProfileInfoKHR */
    { 1000040004, 32 }, /* VideoDecodeH264SessionParametersCreateInfoKHR */
    { 1000040005, 48 }, /* VideoDecodeH264SessionParametersAddInfoKHR */
    { 1000040006, 24 }, /* VideoDecodeH264DpbSlotInfoKHR */
    { 1000044000, 72 }, /* RenderingInfo */
    { 1000044001, 72 }, /* RenderingAttachmentInfo */
    { 1000044002, 40 }, /* PipelineRenderingCreateInfo */
    { 1000044003, 24 }, /* PhysicalDeviceDynamicRenderingFeatures */
    { 1000044004, 56 }, /* CommandBufferInheritanceRenderingInfo */
    { 1000044006, 40 }, /* RenderingFragmentShadingRateAttachmentInfoKHR */
    { 1000044007, 32 }, /* RenderingFragmentDensityMapAttachmentInfoEXT */
    { 1000044008, 40 }, /* AttachmentSampleCountInfoAMD */
    { 1000044009, 24 }, /* MultiviewPerViewAttributesInfoNVX */
    { 1000050000, 24 }, /* PhysicalDeviceCornerSampledImageFeaturesNV */
    { 1000053000, 64 }, /* RenderPassMultiviewCreateInfo */
    { 1000053001, 32 }, /* PhysicalDeviceMultiviewFeatures */
    { 1000053002, 24 }, /* PhysicalDeviceMultiviewProperties */
    { 1000056000, 24 }, /* ExternalMemoryImageCreateInfoNV */
    { 1000056001, 24 }, /* ExportMemoryAllocateInfoNV */
    { 1000059000, 240 }, /* PhysicalDeviceFeatures2 */
    { 1000059001, 840 }, /* PhysicalDeviceProperties2 */
    { 1000059002, 32 }, /* FormatProperties2 */
    { 1000059003, 48 }, /* ImageFormatProperties2 */
    { 1000059004, 40 }, /* PhysicalDeviceImageFormatInfo2 */
    { 1000059005, 40 }, /* QueueFamilyProperties2 */
    { 1000059006, 536 }, /* PhysicalDeviceMemoryProperties2 */
    { 1000059007, 40 }, /* SparseImageFormatProperties2 */
    { 1000059008, 40 }, /* PhysicalDeviceSparseImageFormatInfo2 */
    { 1000060000, 24 }, /* MemoryAllocateFlagsInfo */
    { 1000060003, 32 }, /* DeviceGroupRenderPassBeginInfo */
    { 1000060004, 24 }, /* DeviceGroupCommandBufferBeginInfo */
    { 1000060005, 64 }, /* DeviceGroupSubmitInfo */
    { 1000060006, 24 }, /* DeviceGroupBindSparseInfo */
    { 1000060007, 152 }, /* DeviceGroupPresentCapabilitiesKHR */
    { 1000060008, 24 }, /* ImageSwapchainCreateInfoKHR */
    { 1000060009, 32 }, /* BindImageMemorySwapchainInfoKHR */
    { 1000060010, 56 }, /* AcquireNextImageInfoKHR */
    { 1000060011, 40 }, /* DeviceGroupPresentInfoKHR */
    { 1000060012, 24 }, /* DeviceGroupSwapchainCreateInfoKHR */
    { 1000060013, 32 }, /* BindBufferMemoryDeviceGroupInfo */
    { 1000060014, 48 }, /* BindImageMemoryDeviceGroupInfo */
    { 1000061000, 32 }, /* ValidationFlagsEXT */
    { 1000063000, 24 }, /* PhysicalDeviceShaderDrawParametersFeatures */
    { 1000068000, 32 }, /* PipelineRobustnessCreateInfo */
    { 1000068001, 24 }, /* PhysicalDevicePipelineRobustnessFeatures */
    { 1000068002, 32 }, /* PhysicalDevicePipelineRobustnessProperties */
    { 1000070000, 288 }, /* PhysicalDeviceGroupProperties */
    { 1000070001, 32 }, /* DeviceGroupDeviceCreateInfo */
    { 1000071000, 24 }, /* PhysicalDeviceExternalImageFormatInfo */
    { 1000071001, 32 }, /* ExternalImageFormatProperties */
    { 1000071002, 32 }, /* PhysicalDeviceExternalBufferInfo */
    { 1000071003, 32 }, /* ExternalBufferProperties */
    { 1000072000, 24 }, /* ExternalMemoryBufferCreateInfo */
    { 1000072001, 24 }, /* ExternalMemoryImageCreateInfo */
    { 1000072002, 24 }, /* ExportMemoryAllocateInfo */
    { 1000074000, 24 }, /* ImportMemoryFdInfoKHR */
    { 1000074001, 24 }, /* MemoryFdPropertiesKHR */
    { 1000074002, 32 }, /* MemoryGetFdInfoKHR */
    { 1000076000, 24 }, /* PhysicalDeviceExternalSemaphoreInfo */
    { 1000076001, 32 }, /* ExternalSemaphoreProperties */
    { 1000077000, 24 }, /* ExportSemaphoreCreateInfo */
    { 1000079000, 40 }, /* ImportSemaphoreFdInfoKHR */
    { 1000079001, 32 }, /* SemaphoreGetFdInfoKHR */
    { 1000080000, 24 }, /* PhysicalDevicePushDescriptorProperties */
    { 1000081000, 24 }, /* CommandBufferInheritanceConditionalRenderingInfoEXT */
    { 1000081001, 24 }, /* PhysicalDeviceConditionalRenderingFeaturesEXT */
    { 1000081002, 40 }, /* ConditionalRenderingBeginInfoEXT */
    { 1000082000, 24 }, /* PhysicalDeviceShaderFloat16Int8Features */
    { 1000084000, 32 }, /* PresentRegionsKHR */
    { 1000085000, 72 }, /* DescriptorUpdateTemplateCreateInfo */
    { 1000087000, 32 }, /* PipelineViewportWScalingStateCreateInfoNV */
    { 1000090000, 72 }, /* SurfaceCapabilities2EXT */
    { 1000091000, 24 }, /* DisplayPowerInfoEXT */
    { 1000091001, 24 }, /* DeviceEventInfoEXT */
    { 1000091002, 24 }, /* DisplayEventInfoEXT */
    { 1000091003, 24 }, /* SwapchainCounterCreateInfoEXT */
    { 1000092000, 32 }, /* PresentTimesInfoGOOGLE */
    { 1000094000, 32 }, /* PhysicalDeviceSubgroupProperties */
    { 1000097000, 24 }, /* PhysicalDeviceMultiviewPerViewAttributesPropertiesNVX */
    { 1000098000, 32 }, /* PipelineViewportSwizzleStateCreateInfoNV */
    { 1000099000, 24 }, /* PhysicalDeviceDiscardRectanglePropertiesEXT */
    { 1000099001, 40 }, /* PipelineDiscardRectangleStateCreateInfoEXT */
    { 1000101000, 56 }, /* PhysicalDeviceConservativeRasterizationPropertiesEXT */
    { 1000101001, 32 }, /* PipelineRasterizationConservativeStateCreateInfoEXT */
    { 1000102000, 24 }, /* PhysicalDeviceDepthClipEnableFeaturesEXT */
    { 1000102001, 24 }, /* PipelineRasterizationDepthClipStateCreateInfoEXT */
    { 1000105000, 64 }, /* HdrMetadataEXT */
    { 1000108000, 24 }, /* PhysicalDeviceImagelessFramebufferFeatures */
    { 1000108001, 32 }, /* FramebufferAttachmentsCreateInfo */
    { 1000108002, 48 }, /* FramebufferAttachmentImageInfo */
    { 1000108003, 32 }, /* RenderPassAttachmentBeginInfo */
    { 1000109000, 56 }, /* AttachmentDescription2 */
    { 1000109001, 32 }, /* AttachmentReference2 */
    { 1000109002, 88 }, /* SubpassDescription2 */
    { 1000109003, 48 }, /* SubpassDependency2 */
    { 1000109004, 80 }, /* RenderPassCreateInfo2 */
    { 1000109005, 24 }, /* SubpassBeginInfo */
    { 1000109006, 16 }, /* SubpassEndInfo */
    { 1000110000, 24 }, /* PhysicalDeviceRelaxedLineRasterizationFeaturesIMG */
    { 1000111000, 24 }, /* SharedPresentSurfaceCapabilitiesKHR */
    { 1000112000, 24 }, /* PhysicalDeviceExternalFenceInfo */
    { 1000112001, 32 }, /* ExternalFenceProperties */
    { 1000113000, 24 }, /* ExportFenceCreateInfo */
    { 1000115000, 40 }, /* ImportFenceFdInfoKHR */
    { 1000115001, 32 }, /* FenceGetFdInfoKHR */
    { 1000116000, 24 }, /* PhysicalDevicePerformanceQueryFeaturesKHR */
    { 1000116001, 24 }, /* PhysicalDevicePerformanceQueryPropertiesKHR */
    { 1000116002, 32 }, /* QueryPoolPerformanceCreateInfoKHR */
    { 1000116003, 24 }, /* PerformanceQuerySubmitInfoKHR */
    { 1000116004, 32 }, /* AcquireProfilingLockInfoKHR */
    { 1000116005, 48 }, /* PerformanceCounterKHR */
    { 1000116006, 792 }, /* PerformanceCounterDescriptionKHR */
    { 1000117000, 24 }, /* PhysicalDevicePointClippingProperties */
    { 1000117001, 32 }, /* RenderPassInputAttachmentAspectCreateInfo */
    { 1000117002, 24 }, /* ImageViewUsageCreateInfo */
    { 1000117003, 24 }, /* PipelineTessellationDomainOriginStateCreateInfo */
    { 1000119000, 24 }, /* PhysicalDeviceSurfaceInfo2KHR */
    { 1000119001, 72 }, /* SurfaceCapabilities2KHR */
    { 1000119002, 24 }, /* SurfaceFormat2KHR */
    { 1000120000, 24 }, /* PhysicalDeviceVariablePointersFeatures */
    { 1000121000, 64 }, /* DisplayProperties2KHR */
    { 1000121001, 32 }, /* DisplayPlaneProperties2KHR */
    { 1000121002, 40 }, /* DisplayModeProperties2KHR */
    { 1000121003, 32 }, /* DisplayPlaneInfo2KHR */
    { 1000121004, 88 }, /* DisplayPlaneCapabilities2KHR */
    { 1000127000, 24 }, /* MemoryDedicatedRequirements */
    { 1000127001, 32 }, /* MemoryDedicatedAllocateInfo */
    { 1000128000, 40 }, /* DebugUtilsObjectNameInfoEXT */
    { 1000128001, 56 }, /* DebugUtilsObjectTagInfoEXT */
    { 1000128002, 40 }, /* DebugUtilsLabelEXT */
    { 1000128003, 96 }, /* DebugUtilsMessengerCallbackDataEXT */
    { 1000128004, 48 }, /* DebugUtilsMessengerCreateInfoEXT */
    { 1000130000, 24 }, /* PhysicalDeviceSamplerFilterMinmaxProperties */
    { 1000130001, 24 }, /* SamplerReductionModeCreateInfo */
    { 1000138000, 24 }, /* PhysicalDeviceInlineUniformBlockFeatures */
    { 1000138001, 40 }, /* PhysicalDeviceInlineUniformBlockProperties */
    { 1000138002, 32 }, /* WriteDescriptorSetInlineUniformBlock */
    { 1000138003, 24 }, /* DescriptorPoolInlineUniformBlockCreateInfo */
    { 1000143000, 40 }, /* SampleLocationsInfoEXT */
    { 1000143001, 48 }, /* RenderPassSampleLocationsBeginInfoEXT */
    { 1000143002, 64 }, /* PipelineSampleLocationsStateCreateInfoEXT */
    { 1000143003, 48 }, /* PhysicalDeviceSampleLocationsPropertiesEXT */
    { 1000143004, 24 }, /* MultisamplePropertiesEXT */
    { 1000145000, 24 }, /* ProtectedSubmitInfo */
    { 1000145001, 24 }, /* PhysicalDeviceProtectedMemoryFeatures */
    { 1000145002, 24 }, /* PhysicalDeviceProtectedMemoryProperties */
    { 1000145003, 32 }, /* DeviceQueueInfo2 */
    { 1000146000, 24 }, /* BufferMemoryRequirementsInfo2 */
    { 1000146001, 24 }, /* ImageMemoryRequirementsInfo2 */
    { 1000146002, 24 }, /* ImageSparseMemoryRequirementsInfo2 */
    { 1000146003, 40 }, /* MemoryRequirements2 */
    { 1000146004, 64 }, /* SparseImageMemoryRequirements2 */
    { 1000147000, 32 }, /* ImageFormatListCreateInfo */
    { 1000148000, 24 }, /* PhysicalDeviceBlendOperationAdvancedFeaturesEXT */
    { 1000148001, 40 }, /* PhysicalDeviceBlendOperationAdvancedPropertiesEXT */
    { 1000148002, 32 }, /* PipelineColorBlendAdvancedStateCreateInfoEXT */
    { 1000149000, 32 }, /* PipelineCoverageToColorStateCreateInfoNV */
    { 1000150000, 80 }, /* AccelerationStructureBuildGeometryInfoKHR */
    { 1000150002, 24 }, /* AccelerationStructureDeviceAddressInfoKHR */
    { 1000150003, 32 }, /* AccelerationStructureGeometryAabbsDataKHR */
    { 1000150004, 32 }, /* AccelerationStructureGeometryInstancesDataKHR */
    { 1000150005, 64 }, /* AccelerationStructureGeometryTrianglesDataKHR */
    { 1000150006, 96 }, /* AccelerationStructureGeometryKHR */
    { 1000150007, 32 }, /* WriteDescriptorSetAccelerationStructureKHR */
    { 1000150009, 24 }, /* AccelerationStructureVersionInfoKHR */
    { 1000150010, 40 }, /* CopyAccelerationStructureInfoKHR */
    { 1000150011, 40 }, /* CopyAccelerationStructureToMemoryInfoKHR */
    { 1000150012, 40 }, /* CopyMemoryToAccelerationStructureInfoKHR */
    { 1000150013, 40 }, /* PhysicalDeviceAccelerationStructureFeaturesKHR */
    { 1000150014, 64 }, /* PhysicalDeviceAccelerationStructurePropertiesKHR */
    { 1000150015, 104 }, /* RayTracingPipelineCreateInfoKHR */
    { 1000150016, 48 }, /* RayTracingShaderGroupCreateInfoKHR */
    { 1000150017, 64 }, /* AccelerationStructureCreateInfoKHR */
    { 1000150018, 24 }, /* RayTracingPipelineInterfaceCreateInfoKHR */
    { 1000150020, 40 }, /* AccelerationStructureBuildSizesInfoKHR */
    { 1000152000, 40 }, /* PipelineCoverageModulationStateCreateInfoNV */
    { 1000156000, 64 }, /* SamplerYcbcrConversionCreateInfo */
    { 1000156001, 24 }, /* SamplerYcbcrConversionInfo */
    { 1000156002, 24 }, /* BindImagePlaneMemoryInfo */
    { 1000156003, 24 }, /* ImagePlaneMemoryRequirementsInfo */
    { 1000156004, 24 }, /* PhysicalDeviceSamplerYcbcrConversionFeatures */
    { 1000156005, 24 }, /* SamplerYcbcrConversionImageFormatProperties */
    { 1000157000, 40 }, /* BindBufferMemoryInfo */
    { 1000157001, 40 }, /* BindImageMemoryInfo */
    { 1000158000, 32 }, /* DrmFormatModifierPropertiesListEXT */
    { 1000158002, 40 }, /* PhysicalDeviceImageDrmFormatModifierInfoEXT */
    { 1000158003, 32 }, /* ImageDrmFormatModifierListCreateInfoEXT */
    { 1000158004, 40 }, /* ImageDrmFormatModifierExplicitCreateInfoEXT */
    { 1000158005, 24 }, /* ImageDrmFormatModifierPropertiesEXT */
    { 1000158006, 32 }, /* DrmFormatModifierPropertiesList2EXT */
    { 1000160000, 40 }, /* ValidationCacheCreateInfoEXT */
    { 1000160001, 24 }, /* ShaderModuleValidationCacheCreateInfoEXT */
    { 1000161000, 32 }, /* DescriptorSetLayoutBindingFlagsCreateInfo */
    { 1000161001, 96 }, /* PhysicalDeviceDescriptorIndexingFeatures */
    { 1000161002, 112 }, /* PhysicalDeviceDescriptorIndexingProperties */
    { 1000161003, 32 }, /* DescriptorSetVariableDescriptorCountAllocateInfo */
    { 1000161004, 24 }, /* DescriptorSetVariableDescriptorCountLayoutSupport */
    { 1000164000, 32 }, /* PipelineViewportShadingRateImageStateCreateInfoNV */
    { 1000164001, 24 }, /* PhysicalDeviceShadingRateImageFeaturesNV */
    { 1000164002, 32 }, /* PhysicalDeviceShadingRateImagePropertiesNV */
    { 1000164005, 32 }, /* PipelineViewportCoarseSampleOrderStateCreateInfoNV */
    { 1000165000, 80 }, /* RayTracingPipelineCreateInfoNV */
    { 1000165001, 64 }, /* AccelerationStructureCreateInfoNV */
    { 1000165003, 168 }, /* GeometryNV */
    { 1000165004, 96 }, /* GeometryTrianglesNV */
    { 1000165006, 56 }, /* BindAccelerationStructureMemoryInfoNV */
    { 1000165007, 32 }, /* WriteDescriptorSetAccelerationStructureNV */
    { 1000165008, 32 }, /* AccelerationStructureMemoryRequirementsInfoNV */
    { 1000165009, 64 }, /* PhysicalDeviceRayTracingPropertiesNV */
    { 1000165011, 40 }, /* RayTracingShaderGroupCreateInfoNV */
    { 1000165012, 40 }, /* AccelerationStructureInfoNV */
    { 1000166000, 24 }, /* PhysicalDeviceRepresentativeFragmentTestFeaturesNV */
    { 1000166001, 24 }, /* PipelineRepresentativeFragmentTestStateCreateInfoNV */
    { 1000168000, 32 }, /* PhysicalDeviceMaintenance3Properties */
    { 1000168001, 24 }, /* DescriptorSetLayoutSupport */
    { 1000170000, 24 }, /* PhysicalDeviceImageViewImageFormatInfoEXT */
    { 1000170001, 24 }, /* FilterCubicImageViewImageFormatPropertiesEXT */
    { 1000174000, 24 }, /* DeviceQueueGlobalPriorityCreateInfo */
    { 1000175000, 24 }, /* PhysicalDeviceShaderSubgroupExtendedTypesFeatures */
    { 1000178000, 32 }, /* ImportMemoryHostPointerInfoEXT */
    { 1000178001, 24 }, /* MemoryHostPointerPropertiesEXT */
    { 1000178002, 24 }, /* PhysicalDeviceExternalMemoryHostPropertiesEXT */
    { 1000180000, 24 }, /* PhysicalDeviceShaderAtomicInt64Features */
    { 1000181000, 24 }, /* PhysicalDeviceShaderClockFeaturesKHR */
    { 1000183000, 24 }, /* PipelineCompilerControlCreateInfoAMD */
    { 1000184000, 24 }, /* CalibratedTimestampInfoKHR */
    { 1000185000, 72 }, /* PhysicalDeviceShaderCorePropertiesAMD */
    { 1000187000, 24 }, /* VideoDecodeH265CapabilitiesKHR */
    { 1000187001, 40 }, /* VideoDecodeH265SessionParametersCreateInfoKHR */
    { 1000187002, 64 }, /* VideoDecodeH265SessionParametersAddInfoKHR */
    { 1000187003, 24 }, /* VideoDecodeH265ProfileInfoKHR */
    { 1000187004, 40 }, /* VideoDecodeH265PictureInfoKHR */
    { 1000187005, 24 }, /* VideoDecodeH265DpbSlotInfoKHR */
    { 1000189000, 24 }, /* DeviceMemoryOverallocationCreateInfoAMD */
    { 1000190000, 24 }, /* PhysicalDeviceVertexAttributeDivisorPropertiesEXT */
    { 1000190001, 32 }, /* PipelineVertexInputDivisorStateCreateInfo */
    { 1000190002, 24 }, /* PhysicalDeviceVertexAttributeDivisorFeatures */
    { 1000192000, 40 }, /* PipelineCreationFeedbackCreateInfo */
    { 1000196000, 536 }, /* PhysicalDeviceDriverProperties */
    { 1000197000, 88 }, /* PhysicalDeviceFloatControlsProperties */
    { 1000199000, 32 }, /* PhysicalDeviceDepthStencilResolveProperties */
    { 1000199001, 32 }, /* SubpassDescriptionDepthStencilResolve */
    { 1000201000, 24 }, /* PhysicalDeviceComputeShaderDerivativesFeaturesKHR */
    { 1000202000, 24 }, /* PhysicalDeviceMeshShaderFeaturesNV */
    { 1000202001, 88 }, /* PhysicalDeviceMeshShaderPropertiesNV */
    { 1000203000, 24 }, /* PhysicalDeviceFragmentShaderBarycentricFeaturesKHR */
    { 1000204000, 24 }, /* PhysicalDeviceShaderImageFootprintFeaturesNV */
    { 1000205000, 32 }, /* PipelineViewportExclusiveScissorStateCreateInfoNV */
    { 1000205002, 24 }, /* PhysicalDeviceExclusiveScissorFeaturesNV */
    { 1000206000, 32 }, /* CheckpointDataNV */
    { 1000206001, 24 }, /* QueueFamilyCheckpointPropertiesNV */
    { 1000207000, 24 }, /* PhysicalDeviceTimelineSemaphoreFeatures */
    { 1000207001, 24 }, /* PhysicalDeviceTimelineSemaphoreProperties */
    { 1000207002, 32 }, /* SemaphoreTypeCreateInfo */
    { 1000207003, 48 }, /* TimelineSemaphoreSubmitInfo */
    { 1000207004, 40 }, /* SemaphoreWaitInfo */
    { 1000207005, 32 }, /* SemaphoreSignalInfo */
    { 1000209000, 24 }, /* PhysicalDeviceShaderIntegerFunctions2FeaturesINTEL */
    { 1000210000, 24 }, /* QueryPoolPerformanceQueryCreateInfoINTEL */
    { 1000210001, 24 }, /* InitializePerformanceApiInfoINTEL */
    { 1000210002, 24 }, /* PerformanceMarkerInfoINTEL */
    { 1000210003, 24 }, /* PerformanceStreamMarkerInfoINTEL */
    { 1000210004, 32 }, /* PerformanceOverrideInfoINTEL */
    { 1000210005, 24 }, /* PerformanceConfigurationAcquireInfoINTEL */
    { 1000211000, 32 }, /* PhysicalDeviceVulkanMemoryModelFeatures */
    { 1000213000, 24 }, /* DisplayNativeHdrSurfaceCapabilitiesAMD */
    { 1000213001, 24 }, /* SwapchainDisplayNativeHdrCreateInfoAMD */
    { 1000215000, 24 }, /* PhysicalDeviceShaderTerminateInvocationFeatures */
    { 1000218000, 32 }, /* PhysicalDeviceFragmentDensityMapFeaturesEXT */
    { 1000218001, 40 }, /* PhysicalDeviceFragmentDensityMapPropertiesEXT */
    { 1000218002, 24 }, /* RenderPassFragmentDensityMapCreateInfoEXT */
    { 1000221000, 24 }, /* PhysicalDeviceScalarBlockLayoutFeatures */
    { 1000225000, 32 }, /* PhysicalDeviceSubgroupSizeControlProperties */
    { 1000225001, 24 }, /* PipelineShaderStageRequiredSubgroupSizeCreateInfo */
    { 1000225002, 24 }, /* PhysicalDeviceSubgroupSizeControlFeatures */
    { 1000226000, 32 }, /* FragmentShadingRateAttachmentInfoKHR */
    { 1000226001, 32 }, /* PipelineFragmentShadingRateStateCreateInfoKHR */
    { 1000226002, 96 }, /* PhysicalDeviceFragmentShadingRatePropertiesKHR */
    { 1000226003, 32 }, /* PhysicalDeviceFragmentShadingRateFeaturesKHR */
    { 1000226004, 32 }, /* PhysicalDeviceFragmentShadingRateKHR */
    { 1000227000, 24 }, /* PhysicalDeviceShaderCoreProperties2AMD */
    { 1000229000, 24 }, /* PhysicalDeviceCoherentMemoryFeaturesAMD */
    { 1000232000, 24 }, /* PhysicalDeviceDynamicRenderingLocalReadFeatures */
    { 1000232001, 32 }, /* RenderingAttachmentLocationInfo */
    { 1000232002, 48 }, /* RenderingInputAttachmentIndexInfo */
    { 1000234000, 24 }, /* PhysicalDeviceShaderImageAtomicInt64FeaturesEXT */
    { 1000235000, 24 }, /* PhysicalDeviceShaderQuadControlFeaturesKHR */
    { 1000237000, 272 }, /* PhysicalDeviceMemoryBudgetPropertiesEXT */
    { 1000238000, 24 }, /* PhysicalDeviceMemoryPriorityFeaturesEXT */
    { 1000238001, 24 }, /* MemoryPriorityAllocateInfoEXT */
    { 1000239000, 24 }, /* SurfaceProtectedCapabilitiesKHR */
    { 1000240000, 24 }, /* PhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV */
    { 1000241000, 24 }, /* PhysicalDeviceSeparateDepthStencilLayoutsFeatures */
    { 1000241001, 24 }, /* AttachmentReferenceStencilLayout */
    { 1000241002, 24 }, /* AttachmentDescriptionStencilLayout */
    { 1000244000, 32 }, /* PhysicalDeviceBufferDeviceAddressFeaturesEXT */
    { 1000244001, 24 }, /* BufferDeviceAddressInfo */
    { 1000244002, 24 }, /* BufferDeviceAddressCreateInfoEXT */
    { 1000245000, 1048 }, /* PhysicalDeviceToolProperties */
    { 1000246000, 24 }, /* ImageStencilUsageCreateInfo */
    { 1000247000, 48 }, /* ValidationFeaturesEXT */
    { 1000248000, 24 }, /* PhysicalDevicePresentWaitFeaturesKHR */
    { 1000249000, 24 }, /* PhysicalDeviceCooperativeMatrixFeaturesNV */
    { 1000249001, 48 }, /* CooperativeMatrixPropertiesNV */
    { 1000249002, 24 }, /* PhysicalDeviceCooperativeMatrixPropertiesNV */
    { 1000250000, 24 }, /* PhysicalDeviceCoverageReductionModeFeaturesNV */
    { 1000250001, 24 }, /* PipelineCoverageReductionStateCreateInfoNV */
    { 1000250002, 32 }, /* FramebufferMixedSamplesCombinationNV */
    { 1000251000, 32 }, /* PhysicalDeviceFragmentShaderInterlockFeaturesEXT */
    { 1000252000, 24 }, /* PhysicalDeviceYcbcrImageArraysFeaturesEXT */
    { 1000253000, 24 }, /* PhysicalDeviceUniformBufferStandardLayoutFeatures */
    { 1000254000, 24 }, /* PhysicalDeviceProvokingVertexFeaturesEXT */
    { 1000254001, 24 }, /* PipelineRasterizationProvokingVertexStateCreateInfoEXT */
    { 1000254002, 24 }, /* PhysicalDeviceProvokingVertexPropertiesEXT */
    { 1000256000, 24 }, /* HeadlessSurfaceCreateInfoEXT */
    { 1000257000, 32 }, /* PhysicalDeviceBufferDeviceAddressFeatures */
    { 1000257002, 24 }, /* BufferOpaqueCaptureAddressCreateInfo */
    { 1000257003, 24 }, /* MemoryOpaqueCaptureAddressAllocateInfo */
    { 1000257004, 24 }, /* DeviceMemoryOpaqueCaptureAddressInfo */
    { 1000259000, 40 }, /* PhysicalDeviceLineRasterizationFeatures */
    { 1000259001, 32 }, /* PipelineRasterizationLineStateCreateInfo */
    { 1000259002, 24 }, /* PhysicalDeviceLineRasterizationProperties */
    { 1000260000, 64 }, /* PhysicalDeviceShaderAtomicFloatFeaturesEXT */
    { 1000261000, 24 }, /* PhysicalDeviceHostQueryResetFeatures */
    { 1000265000, 24 }, /* PhysicalDeviceIndexTypeUint8Features */
    { 1000267000, 24 }, /* PhysicalDeviceExtendedDynamicStateFeaturesEXT */
    { 1000269000, 24 }, /* PhysicalDevicePipelineExecutablePropertiesFeaturesKHR */
    { 1000269001, 24 }, /* PipelineInfoKHR */
    { 1000269002, 536 }, /* PipelineExecutablePropertiesKHR */
    { 1000269003, 32 }, /* PipelineExecutableInfoKHR */
    { 1000269004, 544 }, /* PipelineExecutableStatisticKHR */
    { 1000269005, 552 }, /* PipelineExecutableInternalRepresentationKHR */
    { 1000270000, 24 }, /* PhysicalDeviceHostImageCopyFeatures */
    { 1000270001, 72 }, /* PhysicalDeviceHostImageCopyProperties */
    { 1000270002, 72 }, /* MemoryToImageCopy */
    { 1000270003, 72 }, /* ImageToMemoryCopy */
    { 1000270004, 48 }, /* CopyImageToMemoryInfo */
    { 1000270005, 48 }, /* CopyMemoryToImageInfo */
    { 1000270006, 56 }, /* HostImageLayoutTransitionInfo */
    { 1000270007, 64 }, /* CopyImageToImageInfo */
    { 1000270008, 24 }, /* SubresourceHostMemcpySize */
    { 1000270009, 24 }, /* HostImageCopyDevicePerformanceQuery */
    { 1000271000, 48 }, /* MemoryMapInfo */
    { 1000271001, 32 }, /* MemoryUnmapInfo */
    { 1000272000, 32 }, /* PhysicalDeviceMapMemoryPlacedFeaturesEXT */
    { 1000272001, 24 }, /* PhysicalDeviceMapMemoryPlacedPropertiesEXT */
    { 1000272002, 24 }, /* MemoryMapPlacedInfoEXT */
    { 1000273000, 64 }, /* PhysicalDeviceShaderAtomicFloat2FeaturesEXT */
    { 1000274000, 24 }, /* SurfacePresentModeEXT */
    { 1000274001, 48 }, /* SurfacePresentScalingCapabilitiesEXT */
    { 1000274002, 32 }, /* SurfacePresentModeCompatibilityEXT */
    { 1000275000, 24 }, /* PhysicalDeviceSwapchainMaintenance1FeaturesEXT */
    { 1000275001, 32 }, /* SwapchainPresentFenceInfoEXT */
    { 1000275002, 32 }, /* SwapchainPresentModesCreateInfoEXT */
    { 1000275003, 32 }, /* SwapchainPresentModeInfoEXT */
    { 1000275004, 32 }, /* SwapchainPresentScalingCreateInfoEXT */
    { 1000275005, 40 }, /* ReleaseSwapchainImagesInfoEXT */
    { 1000276000, 24 }, /* PhysicalDeviceShaderDemoteToHelperInvocationFeatures */
    { 1000277000, 56 }, /* PhysicalDeviceDeviceGeneratedCommandsPropertiesNV */
    { 1000277001, 48 }, /* GraphicsShaderGroupCreateInfoNV */
    { 1000277002, 48 }, /* GraphicsPipelineShaderGroupsCreateInfoNV */
    { 1000277003, 88 }, /* IndirectCommandsLayoutTokenNV */
    { 1000277004, 56 }, /* IndirectCommandsLayoutCreateInfoNV */
    { 1000277005, 120 }, /* GeneratedCommandsInfoNV */
    { 1000277006, 48 }, /* GeneratedCommandsMemoryRequirementsInfoNV */
    { 1000277007, 24 }, /* PhysicalDeviceDeviceGeneratedCommandsFeaturesNV */
    { 1000278000, 24 }, /* PhysicalDeviceInheritedViewportScissorFeaturesNV */
    { 1000278001, 32 }, /* CommandBufferInheritanceViewportScissorInfoNV */
    { 1000280000, 24 }, /* PhysicalDeviceShaderIntegerDotProductFeatures */
    { 1000280001, 136 }, /* PhysicalDeviceShaderIntegerDotProductProperties */
    { 1000281000, 24 }, /* PhysicalDeviceTexelBufferAlignmentFeaturesEXT */
    { 1000281001, 48 }, /* PhysicalDeviceTexelBufferAlignmentProperties */
    { 1000282000, 40 }, /* CommandBufferInheritanceRenderPassTransformInfoQCOM */
    { 1000282001, 24 }, /* RenderPassTransformBeginInfoQCOM */
    { 1000283000, 32 }, /* PhysicalDeviceDepthBiasControlFeaturesEXT */
    { 1000283001, 32 }, /* DepthBiasInfoEXT */
    { 1000283002, 24 }, /* DepthBiasRepresentationInfoEXT */
    { 1000284000, 24 }, /* PhysicalDeviceDeviceMemoryReportFeaturesEXT */
    { 1000284001, 40 }, /* DeviceDeviceMemoryReportCreateInfoEXT */
    { 1000284002, 64 }, /* DeviceMemoryReportCallbackDataEXT */
    { 1000286000, 32 }, /* PhysicalDeviceRobustness2FeaturesEXT */
    { 1000286001, 32 }, /* PhysicalDeviceRobustness2PropertiesEXT */
    { 1000287000, 40 }, /* SamplerCustomBorderColorCreateInfoEXT */
    { 1000287001, 24 }, /* PhysicalDeviceCustomBorderColorPropertiesEXT */
    { 1000287002, 24 }, /* PhysicalDeviceCustomBorderColorFeaturesEXT */
    { 1000290000, 32 }, /* PipelineLibraryCreateInfoKHR */
    { 1000292000, 24 }, /* PhysicalDevicePresentBarrierFeaturesNV */
    { 1000292001, 24 }, /* SurfaceCapabilitiesPresentBarrierNV */
    { 1000292002, 24 }, /* SwapchainPresentBarrierCreateInfoNV */
    { 1000294000, 32 }, /* PresentIdKHR */
    { 1000294001, 24 }, /* PhysicalDevicePresentIdFeaturesKHR */
    { 1000295000, 24 }, /* PhysicalDevicePrivateDataFeatures */
    { 1000295001, 24 }, /* DevicePrivateDataCreateInfo */
    { 1000295002, 24 }, /* PrivateDataSlotCreateInfo */
    { 1000297000, 24 }, /* PhysicalDevicePipelineCreationCacheControlFeatures */
    { 1000299000, 128 }, /* VideoEncodeInfoKHR */
    { 1000299001, 48 }, /* VideoEncodeRateControlInfoKHR */
    { 1000299002, 40 }, /* VideoEncodeRateControlLayerInfoKHR */
    { 1000299003, 56 }, /* VideoEncodeCapabilitiesKHR */
    { 1000299004, 32 }, /* VideoEncodeUsageInfoKHR */
    { 1000299005, 24 }, /* QueryPoolVideoEncodeFeedbackCreateInfoKHR */
    { 1000299006, 32 }, /* PhysicalDeviceVideoEncodeQualityLevelInfoKHR */
    { 1000299007, 24 }, /* VideoEncodeQualityLevelPropertiesKHR */
    { 1000299008, 24 }, /* VideoEncodeQualityLevelInfoKHR */
    { 1000299009, 24 }, /* VideoEncodeSessionParametersGetInfoKHR */
    { 1000299010, 24 }, /* VideoEncodeSessionParametersFeedbackInfoKHR */
    { 1000300000, 24 }, /* PhysicalDeviceDiagnosticsConfigFeaturesNV */
    { 1000300001, 24 }, /* DeviceDiagnosticsConfigCreateInfoNV */
    { 1000307000, 32 }, /* CudaModuleCreateInfoNV */
    { 1000307001, 32 }, /* CudaFunctionCreateInfoNV */
    { 1000307002, 88 }, /* CudaLaunchInfoNV */
    { 1000307003, 24 }, /* PhysicalDeviceCudaKernelLaunchFeaturesNV */
    { 1000307004, 24 }, /* PhysicalDeviceCudaKernelLaunchPropertiesNV */
    { 1000310000, 24 }, /* QueryLowLatencySupportNV */
    { 1000314000, 48 }, /* MemoryBarrier2 */
    { 1000314001, 80 }, /* BufferMemoryBarrier2 */
    { 1000314002, 96 }, /* ImageMemoryBarrier2 */
    { 1000314003, 64 }, /* DependencyInfo */
    { 1000314004, 64 }, /* SubmitInfo2 */
    { 1000314005, 48 }, /* SemaphoreSubmitInfo */
    { 1000314006, 32 }, /* CommandBufferSubmitInfo */
    { 1000314007, 24 }, /* PhysicalDeviceSynchronization2Features */
    { 1000314008, 24 }, /* QueueFamilyCheckpointProperties2NV */
    { 1000314009, 32 }, /* CheckpointData2NV */
    { 1000316000, 256 }, /* PhysicalDeviceDescriptorBufferPropertiesEXT */
    { 1000316001, 24 }, /* PhysicalDeviceDescriptorBufferDensityMapPropertiesEXT */
    { 1000316002, 32 }, /* PhysicalDeviceDescriptorBufferFeaturesEXT */
    { 1000316003, 40 }, /* DescriptorAddressInfoEXT */
    { 1000316004, 32 }, /* DescriptorGetInfoEXT */
    { 1000316005, 24 }, /* BufferCaptureDescriptorDataInfoEXT */
    { 1000316006, 24 }, /* ImageCaptureDescriptorDataInfoEXT */
    { 1000316007, 24 }, /* ImageViewCaptureDescriptorDataInfoEXT */
    { 1000316008, 24 }, /* SamplerCaptureDescriptorDataInfoEXT */
    { 1000316009, 32 }, /* AccelerationStructureCaptureDescriptorDataInfoEXT */
    { 1000316010, 24 }, /* OpaqueCaptureDescriptorDataCreateInfoEXT */
    { 1000316011, 32 }, /* DescriptorBufferBindingInfoEXT */
    { 1000316012, 24 }, /* DescriptorBufferBindingPushDescriptorBufferHandleEXT */
    { 1000320000, 24 }, /* PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT */
    { 1000320001, 24 }, /* PhysicalDeviceGraphicsPipelineLibraryPropertiesEXT */
    { 1000320002, 24 }, /* GraphicsPipelineLibraryCreateInfoEXT */
    { 1000321000, 24 }, /* PhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD */
    { 1000322000, 24 }, /* PhysicalDeviceFragmentShaderBarycentricPropertiesKHR */
    { 1000323000, 24 }, /* PhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR */
    { 1000325000, 24 }, /* PhysicalDeviceZeroInitializeWorkgroupMemoryFeatures */
    { 1000326000, 24 }, /* PhysicalDeviceFragmentShadingRateEnumsPropertiesNV */
    { 1000326001, 32 }, /* PhysicalDeviceFragmentShadingRateEnumsFeaturesNV */
    { 1000326002, 32 }, /* PipelineFragmentShadingRateEnumStateCreateInfoNV */
    { 1000327000, 24 }, /* AccelerationStructureGeometryMotionTrianglesDataNV */
    { 1000327001, 24 }, /* PhysicalDeviceRayTracingMotionBlurFeaturesNV */
    { 1000327002, 24 }, /* AccelerationStructureMotionInfoNV */
    { 1000328000, 40 }, /* PhysicalDeviceMeshShaderFeaturesEXT */
    { 1000328001, 160 }, /* PhysicalDeviceMeshShaderPropertiesEXT */
    { 1000330000, 24 }, /* PhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT */
    { 1000332000, 24 }, /* PhysicalDeviceFragmentDensityMap2FeaturesEXT */
    { 1000332001, 32 }, /* PhysicalDeviceFragmentDensityMap2PropertiesEXT */
    { 1000333000, 24 }, /* CopyCommandTransformInfoQCOM */
    { 1000335000, 24 }, /* PhysicalDeviceImageRobustnessFeatures */
    { 1000336000, 32 }, /* PhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR */
    { 1000337000, 48 }, /* CopyBufferInfo2 */
    { 1000337001, 56 }, /* CopyImageInfo2 */
    { 1000337002, 48 }, /* CopyBufferToImageInfo2 */
    { 1000337003, 56 }, /* CopyImageToBufferInfo2 */
    { 1000337004, 64 }, /* BlitImageInfo2 */
    { 1000337005, 56 }, /* ResolveImageInfo2 */
    { 1000337006, 40 }, /* BufferCopy2 */
    { 1000337007, 88 }, /* ImageCopy2 */
    { 1000337008, 96 }, /* ImageBlit2 */
    { 1000337009, 72 }, /* BufferImageCopy2 */
    { 1000337010, 88 }, /* ImageResolve2 */
    { 1000338000, 24 }, /* PhysicalDeviceImageCompressionControlFeaturesEXT */
    { 1000338001, 32 }, /* ImageCompressionControlEXT */
    { 1000338002, 56 }, /* SubresourceLayout2 */
    { 1000338003, 32 }, /* ImageSubresource2 */
    { 1000338004, 24 }, /* ImageCompressionPropertiesEXT */
    { 1000339000, 24 }, /* PhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT */
    { 1000340000, 24 }, /* PhysicalDevice4444FormatsFeaturesEXT */
    { 1000341000, 24 }, /* PhysicalDeviceFaultFeaturesEXT */
    { 1000341001, 32 }, /* DeviceFaultCountsEXT */
    { 1000341002, 296 }, /* DeviceFaultInfoEXT */
    { 1000342000, 32 }, /* PhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT */
    { 1000347000, 40 }, /* PhysicalDeviceRayTracingPipelineFeaturesKHR */
    { 1000347001, 48 }, /* PhysicalDeviceRayTracingPipelinePropertiesKHR */
    { 1000348013, 24 }, /* PhysicalDeviceRayQueryFeaturesKHR */
    { 1000351000, 24 }, /* PhysicalDeviceMutableDescriptorTypeFeaturesEXT */
    { 1000351002, 32 }, /* MutableDescriptorTypeCreateInfoEXT */
    { 1000352000, 24 }, /* PhysicalDeviceVertexInputDynamicStateFeaturesEXT */
    { 1000352001, 32 }, /* VertexInputBindingDescription2EXT */
    { 1000352002, 32 }, /* VertexInputAttributeDescription2EXT */
    { 1000353000, 56 }, /* PhysicalDeviceDrmPropertiesEXT */
    { 1000354000, 24 }, /* PhysicalDeviceAddressBindingReportFeaturesEXT */
    { 1000354001, 48 }, /* DeviceAddressBindingCallbackDataEXT */
    { 1000355000, 24 }, /* PhysicalDeviceDepthClipControlFeaturesEXT */
    { 1000355001, 24 }, /* PipelineViewportDepthClipControlCreateInfoEXT */
    { 1000356000, 24 }, /* PhysicalDevicePrimitiveTopologyListRestartFeaturesEXT */
    { 1000360000, 40 }, /* FormatProperties3 */
    { 1000361000, 24 }, /* PhysicalDevicePresentModeFifoLatestReadyFeaturesEXT */
    { 1000369000, 32 }, /* SubpassShadingPipelineCreateInfoHUAWEI */
    { 1000369001, 24 }, /* PhysicalDeviceSubpassShadingFeaturesHUAWEI */
    { 1000369002, 24 }, /* PhysicalDeviceSubpassShadingPropertiesHUAWEI */
    { 1000370000, 24 }, /* PhysicalDeviceInvocationMaskFeaturesHUAWEI */
    { 1000371000, 32 }, /* MemoryGetRemoteAddressInfoNV */
    { 1000372000, 32 }, /* PipelinePropertiesIdentifierEXT */
    { 1000372001, 24 }, /* PhysicalDevicePipelinePropertiesFeaturesEXT */
    { 1000375000, 24 }, /* PhysicalDeviceFrameBoundaryFeaturesEXT */
    { 1000375001, 88 }, /* FrameBoundaryEXT */
    { 1000376000, 24 }, /* PhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT */
    { 1000376001, 24 }, /* SubpassResolvePerformanceQueryEXT */
    { 1000376002, 24 }, /* MultisampledRenderToSingleSampledInfoEXT */
    { 1000377000, 32 }, /* PhysicalDeviceExtendedDynamicState2FeaturesEXT */
    { 1000381000, 24 }, /* PhysicalDeviceColorWriteEnableFeaturesEXT */
    { 1000381001, 32 }, /* PipelineColorWriteCreateInfoEXT */
    { 1000382000, 32 }, /* PhysicalDevicePrimitivesGeneratedQueryFeaturesEXT */
    { 1000386000, 24 }, /* PhysicalDeviceRayTracingMaintenance1FeaturesKHR */
    { 1000388000, 24 }, /* PhysicalDeviceGlobalPriorityQueryFeatures */
    { 1000388001, 88 }, /* QueueFamilyGlobalPriorityProperties */
    { 1000391000, 24 }, /* PhysicalDeviceImageViewMinLodFeaturesEXT */
    { 1000391001, 24 }, /* ImageViewMinLodCreateInfoEXT */
    { 1000392000, 24 }, /* PhysicalDeviceMultiDrawFeaturesEXT */
    { 1000392001, 24 }, /* PhysicalDeviceMultiDrawPropertiesEXT */
    { 1000395000, 32 }, /* PhysicalDeviceShaderTileImageFeaturesEXT */
    { 1000395001, 32 }, /* PhysicalDeviceShaderTileImagePropertiesEXT */
    { 1000396000, 96 }, /* MicromapBuildInfoEXT */
    { 1000396001, 24 }, /* MicromapVersionInfoEXT */
    { 1000396002, 40 }, /* CopyMicromapInfoEXT */
    { 1000396003, 40 }, /* CopyMicromapToMemoryInfoEXT */
    { 1000396004, 40 }, /* CopyMemoryToMicromapInfoEXT */
    { 1000396005, 32 }, /* PhysicalDeviceOpacityMicromapFeaturesEXT */
    { 1000396006, 24 }, /* PhysicalDeviceOpacityMicromapPropertiesEXT */
    { 1000396007, 64 }, /* MicromapCreateInfoEXT */
    { 1000396008, 40 }, /* MicromapBuildSizesInfoEXT */
    { 1000396009, 72 }, /* AccelerationStructureTrianglesOpacityMicromapEXT */
    { 1000404000, 24 }, /* PhysicalDeviceClusterCullingShaderFeaturesHUAWEI */
    { 1000404001, 56 }, /* PhysicalDeviceClusterCullingShaderPropertiesHUAWEI */
    { 1000404002, 24 }, /* PhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI */
    { 1000411000, 24 }, /* PhysicalDeviceBorderColorSwizzleFeaturesEXT */
    { 1000411001, 40 }, /* SamplerBorderColorComponentMappingCreateInfoEXT */
    { 1000412000, 24 }, /* PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT */
    { 1000413000, 24 }, /* PhysicalDeviceMaintenance4Features */
    { 1000413001, 24 }, /* PhysicalDeviceMaintenance4Properties */
    { 1000413002, 24 }, /* DeviceBufferMemoryRequirements */
    { 1000413003, 32 }, /* DeviceImageMemoryRequirements */
    { 1000415000, 32 }, /* PhysicalDeviceShaderCorePropertiesARM */
    { 1000416000, 24 }, /* PhysicalDeviceShaderSubgroupRotateFeatures */
    { 1000417000, 24 }, /* DeviceQueueShaderCoreControlCreateInfoARM */
    { 1000417001, 24 }, /* PhysicalDeviceSchedulingControlsFeaturesARM */
    { 1000417002, 24 }, /* PhysicalDeviceSchedulingControlsPropertiesARM */
    { 1000418001, 24 }, /* ImageViewSlicedCreateInfoEXT */
    { 1000420000, 24 }, /* PhysicalDeviceDescriptorSetHostMappingFeaturesVALVE */
    { 1000420001, 32 }, /* DescriptorSetBindingReferenceVALVE */
    { 1000420002, 32 }, /* DescriptorSetLayoutHostMappingInfoVALVE */
    { 1000421000, 24 }, /* PhysicalDeviceDepthClampZeroOneFeaturesKHR */
    { 1000422000, 24 }, /* PhysicalDeviceNonSeamlessCubeMapFeaturesEXT */
    { 1000424000, 24 }, /* PhysicalDeviceRenderPassStripedFeaturesARM */
    { 1000424001, 32 }, /* PhysicalDeviceRenderPassStripedPropertiesARM */
    { 1000424002, 32 }, /* RenderPassStripeBeginInfoARM */
    { 1000424003, 32 }, /* RenderPassStripeInfoARM */
    { 1000424004, 32 }, /* RenderPassStripeSubmitInfoARM */
    { 1000425000, 24 }, /* PhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM */
    { 1000425001, 24 }, /* PhysicalDeviceFragmentDensityMapOffsetPropertiesQCOM */
    { 1000425002, 32 }, /* SubpassFragmentDensityMapOffsetEndInfoQCOM */
    { 1000426000, 24 }, /* PhysicalDeviceCopyMemoryIndirectFeaturesNV */
    { 1000426001, 24 }, /* PhysicalDeviceCopyMemoryIndirectPropertiesNV */
    { 1000427000, 24 }, /* PhysicalDeviceMemoryDecompressionFeaturesNV */
    { 1000427001, 32 }, /* PhysicalDeviceMemoryDecompressionPropertiesNV */
    { 1000428000, 32 }, /* PhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV */
    { 1000428001, 40 }, /* ComputePipelineIndirectBufferInfoNV */
    { 1000428002, 32 }, /* PipelineIndirectDeviceAddressInfoNV */
    { 1000429008, 24 }, /* PhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV */
    { 1000429009, 96 }, /* AccelerationStructureGeometryLinearSweptSpheresDataNV */
    { 1000429010, 88 }, /* AccelerationStructureGeometrySpheresDataNV */
    { 1000430000, 24 }, /* PhysicalDeviceLinearColorAttachmentFeaturesNV */
    { 1000434000, 24 }, /* PhysicalDeviceShaderMaximalReconvergenceFeaturesKHR */
    { 1000437000, 24 }, /* PhysicalDeviceImageCompressionControlSwapchainFeaturesEXT */
    { 1000440000, 32 }, /* PhysicalDeviceImageProcessingFeaturesQCOM */
    { 1000440001, 48 }, /* PhysicalDeviceImageProcessingPropertiesQCOM */
    { 1000440002, 40 }, /* ImageViewSampleWeightCreateInfoQCOM */
    { 1000451000, 32 }, /* PhysicalDeviceNestedCommandBufferFeaturesEXT */
    { 1000451001, 24 }, /* PhysicalDeviceNestedCommandBufferPropertiesEXT */
    { 1000453000, 24 }, /* ExternalMemoryAcquireUnmodifiedEXT */
    { 1000455000, 144 }, /* PhysicalDeviceExtendedDynamicState3FeaturesEXT */
    { 1000455001, 24 }, /* PhysicalDeviceExtendedDynamicState3PropertiesEXT */
    { 1000458000, 24 }, /* PhysicalDeviceSubpassMergeFeedbackFeaturesEXT */
    { 1000458001, 24 }, /* RenderPassCreationControlEXT */
    { 1000458002, 24 }, /* RenderPassCreationFeedbackCreateInfoEXT */
    { 1000458003, 24 }, /* RenderPassSubpassFeedbackCreateInfoEXT */
    { 1000462000, 24 }, /* PhysicalDeviceShaderModuleIdentifierFeaturesEXT */
    { 1000462001, 32 }, /* PhysicalDeviceShaderModuleIdentifierPropertiesEXT */
    { 1000462002, 32 }, /* PipelineShaderStageModuleIdentifierCreateInfoEXT */
    { 1000462003, 56 }, /* ShaderModuleIdentifierEXT */
    { 1000464000, 24 }, /* PhysicalDeviceOpticalFlowFeaturesNV */
    { 1000464001, 64 }, /* PhysicalDeviceOpticalFlowPropertiesNV */
    { 1000464002, 24 }, /* OpticalFlowImageFormatInfoNV */
    { 1000464003, 24 }, /* OpticalFlowImageFormatPropertiesNV */
    { 1000464004, 56 }, /* OpticalFlowSessionCreateInfoNV */
    { 1000464005, 32 }, /* OpticalFlowExecuteInfoNV */
    { 1000464010, 32 }, /* OpticalFlowSessionCreatePrivateDataInfoNV */
    { 1000465000, 24 }, /* PhysicalDeviceLegacyDitheringFeaturesEXT */
    { 1000466000, 24 }, /* PhysicalDevicePipelineProtectedAccessFeatures */
    { 1000470000, 24 }, /* PhysicalDeviceMaintenance5Features */
    { 1000470001, 40 }, /* PhysicalDeviceMaintenance5Properties */
    { 1000470003, 40 }, /* RenderingAreaInfo */
    { 1000470004, 32 }, /* DeviceImageSubresourceInfo */
    { 1000470005, 24 }, /* PipelineCreateFlags2CreateInfo */
    { 1000470006, 24 }, /* BufferUsageFlags2CreateInfo */
    { 1000476000, 24 }, /* PhysicalDeviceAntiLagFeaturesAMD */
    { 1000476001, 32 }, /* AntiLagDataAMD */
    { 1000476002, 32 }, /* AntiLagPresentationInfoAMD */
    { 1000481000, 24 }, /* PhysicalDeviceRayTracingPositionFetchFeaturesKHR */
    { 1000482000, 24 }, /* PhysicalDeviceShaderObjectFeaturesEXT */
    { 1000482001, 40 }, /* PhysicalDeviceShaderObjectPropertiesEXT */
    { 1000482002, 96 }, /* ShaderCreateInfoEXT */
    { 1000483000, 24 }, /* PhysicalDevicePipelineBinaryFeaturesKHR */
    { 1000483001, 40 }, /* PipelineBinaryCreateInfoKHR */
    { 1000483002, 32 }, /* PipelineBinaryInfoKHR */
    { 1000483003, 56 }, /* PipelineBinaryKeyKHR */
    { 1000483004, 40 }, /* PhysicalDevicePipelineBinaryPropertiesKHR */
    { 1000483005, 24 }, /* ReleaseCapturedPipelineDataInfoKHR */
    { 1000483006, 24 }, /* PipelineBinaryDataInfoKHR */
    { 1000483007, 16 }, /* PipelineCreateInfoKHR */
    { 1000483008, 24 }, /* DevicePipelineBinaryInternalCacheControlKHR */
    { 1000483009, 32 }, /* PipelineBinaryHandlesInfoKHR */
    { 1000484000, 24 }, /* PhysicalDeviceTilePropertiesFeaturesQCOM */
    { 1000484001, 48 }, /* TilePropertiesQCOM */
    { 1000485000, 24 }, /* PhysicalDeviceAmigoProfilingFeaturesSEC */
    { 1000485001, 32 }, /* AmigoProfilingSubmitInfoSEC */
    { 1000488000, 24 }, /* PhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM */
    { 1000490000, 24 }, /* PhysicalDeviceRayTracingInvocationReorderFeaturesNV */
    { 1000490001, 24 }, /* PhysicalDeviceRayTracingInvocationReorderPropertiesNV */
    { 1000491000, 24 }, /* PhysicalDeviceCooperativeVectorFeaturesNV */
    { 1000491001, 32 }, /* PhysicalDeviceCooperativeVectorPropertiesNV */
    { 1000491002, 40 }, /* CooperativeVectorPropertiesNV */
    { 1000491004, 96 }, /* ConvertCooperativeVectorMatrixInfoNV */
    { 1000492000, 24 }, /* PhysicalDeviceExtendedSparseAddressSpaceFeaturesNV */
    { 1000492001, 32 }, /* PhysicalDeviceExtendedSparseAddressSpacePropertiesNV */
    { 1000495000, 24 }, /* PhysicalDeviceLegacyVertexAttributesFeaturesEXT */
    { 1000495001, 24 }, /* PhysicalDeviceLegacyVertexAttributesPropertiesEXT */
    { 1000496000, 32 }, /* LayerSettingsCreateInfoEXT */
    { 1000497000, 24 }, /* PhysicalDeviceShaderCoreBuiltinsFeaturesARM */
    { 1000497001, 32 }, /* PhysicalDeviceShaderCoreBuiltinsPropertiesARM */
    { 1000498000, 24 }, /* PhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT */
    { 1000499000, 24 }, /* PhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT */
    { 1000505000, 32 }, /* LatencySleepModeInfoNV */
    { 1000505001, 32 }, /* LatencySleepInfoNV */
    { 1000505002, 32 }, /* SetLatencyMarkerInfoNV */
    { 1000505003, 32 }, /* GetLatencyMarkerInfoNV */
    { 1000505004, 128 }, /* LatencyTimingsFrameReportNV */
    { 1000505005, 24 }, /* LatencySubmissionPresentIdNV */
    { 1000505006, 24 }, /* OutOfBandQueueTypeInfoNV */
    { 1000505007, 24 }, /* SwapchainLatencyCreateInfoNV */
    { 1000505008, 32 }, /* LatencySurfaceCapabilitiesNV */
    { 1000506000, 24 }, /* PhysicalDeviceCooperativeMatrixFeaturesKHR */
    { 1000506001, 56 }, /* CooperativeMatrixPropertiesKHR */
    { 1000506002, 24 }, /* PhysicalDeviceCooperativeMatrixPropertiesKHR */
    { 1000510000, 24 }, /* PhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM */
    { 1000510001, 32 }, /* MultiviewPerViewRenderAreasRenderPassBeginInfoQCOM */
    { 1000511000, 24 }, /* PhysicalDeviceComputeShaderDerivativesPropertiesKHR */
    { 1000515000, 24 }, /* PhysicalDeviceVideoMaintenance1FeaturesKHR */
    { 1000515001, 32 }, /* VideoInlineQueryInfoKHR */
    { 1000516000, 24 }, /* PhysicalDevicePerStageDescriptorSetFeaturesNV */
    { 1000518000, 24 }, /* PhysicalDeviceImageProcessing2FeaturesQCOM */
    { 1000518001, 24 }, /* PhysicalDeviceImageProcessing2PropertiesQCOM */
    { 1000518002, 32 }, /* SamplerBlockMatchWindowCreateInfoQCOM */
    { 1000519000, 24 }, /* SamplerCubicWeightsCreateInfoQCOM */
    { 1000519001, 24 }, /* PhysicalDeviceCubicWeightsFeaturesQCOM */
    { 1000519002, 24 }, /* BlitImageCubicWeightsInfoQCOM */
    { 1000520000, 24 }, /* PhysicalDeviceYcbcrDegammaFeaturesQCOM */
    { 1000520001, 24 }, /* SamplerYcbcrConversionYcbcrDegammaCreateInfoQCOM */
    { 1000521000, 24 }, /* PhysicalDeviceCubicClampFeaturesQCOM */
    { 1000524000, 24 }, /* PhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT */
    { 1000525000, 24 }, /* PhysicalDeviceVertexAttributeDivisorProperties */
    { 1000528000, 24 }, /* PhysicalDeviceShaderFloatControls2Features */
    { 1000530000, 24 }, /* PhysicalDeviceLayeredDriverPropertiesMSFT */
    { 1000544000, 24 }, /* PhysicalDeviceShaderExpectAssumeFeatures */
    { 1000545000, 24 }, /* PhysicalDeviceMaintenance6Features */
    { 1000545001, 32 }, /* PhysicalDeviceMaintenance6Properties */
    { 1000545002, 24 }, /* BindMemoryStatus */
    { 1000545003, 64 }, /* BindDescriptorSetsInfo */
    { 1000545004, 48 }, /* PushConstantsInfo */
    { 1000545005, 48 }, /* PushDescriptorSetInfo */
    { 1000545006, 48 }, /* PushDescriptorSetWithTemplateInfo */
    { 1000545007, 56 }, /* SetDescriptorBufferOffsetsInfoEXT */
    { 1000545008, 40 }, /* BindDescriptorBufferEmbeddedSamplersInfoEXT */
    { 1000546000, 24 }, /* PhysicalDeviceDescriptorPoolOverallocationFeaturesNV */
    { 1000551000, 24 }, /* DisplaySurfaceStereoCreateInfoNV */
    { 1000551001, 24 }, /* DisplayModeStereoPropertiesNV */
    { 1000553000, 24 }, /* VideoEncodeQuantizationMapCapabilitiesKHR */
    { 1000553001, 24 }, /* VideoFormatQuantizationMapPropertiesKHR */
    { 1000553002, 32 }, /* VideoEncodeQuantizationMapInfoKHR */
    { 1000553003, 24 }, /* VideoEncodeH264QuantizationMapCapabilitiesKHR */
    { 1000553004, 24 }, /* VideoEncodeH265QuantizationMapCapabilitiesKHR */
    { 1000553005, 24 }, /* VideoEncodeQuantizationMapSessionParametersCreateInfoKHR */
    { 1000553006, 24 }, /* VideoFormatH265QuantizationMapPropertiesKHR */
    { 1000553009, 24 }, /* PhysicalDeviceVideoEncodeQuantizationMapFeaturesKHR */
    { 1000555000, 24 }, /* PhysicalDeviceRawAccessChainsFeaturesNV */
    { 1000558000, 24 }, /* PhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR */
    { 1000559000, 24 }, /* PhysicalDeviceCommandBufferInheritanceFeaturesNV */
    { 1000562000, 24 }, /* PhysicalDeviceMaintenance7FeaturesKHR */
    { 1000562001, 48 }, /* PhysicalDeviceMaintenance7PropertiesKHR */
    { 1000562002, 32 }, /* PhysicalDeviceLayeredApiPropertiesListKHR */
    { 1000562003, 288 }, /* PhysicalDeviceLayeredApiPropertiesKHR */
    { 1000562004, 856 }, /* PhysicalDeviceLayeredApiVulkanPropertiesKHR */
    { 1000563000, 24 }, /* PhysicalDeviceShaderAtomicFloat16VectorFeaturesNV */
    { 1000564000, 24 }, /* PhysicalDeviceShaderReplicatedCompositesFeaturesEXT */
    { 1000568000, 24 }, /* PhysicalDeviceRayTracingValidationFeaturesNV */
    { 1000569000, 24 }, /* PhysicalDeviceClusterAccelerationStructureFeaturesNV */
    { 1000569001, 48 }, /* PhysicalDeviceClusterAccelerationStructurePropertiesNV */
    { 1000569002, 24 }, /* ClusterAccelerationStructureClustersBottomLevelInputNV */
    { 1000569003, 48 }, /* ClusterAccelerationStructureTriangleClusterInputNV */
    { 1000569004, 32 }, /* ClusterAccelerationStructureMoveObjectsInputNV */
    { 1000569005, 40 }, /* ClusterAccelerationStructureInputInfoNV */
    { 1000569006, 160 }, /* ClusterAccelerationStructureCommandsInfoNV */
    { 1000569007, 24 }, /* RayTracingPipelineClusterAccelerationStructureCreateInfoNV */
    { 1000570000, 24 }, /* PhysicalDevicePartitionedAccelerationStructureFeaturesNV */
    { 1000570001, 24 }, /* PhysicalDevicePartitionedAccelerationStructurePropertiesNV */
    { 1000570002, 32 }, /* WriteDescriptorSetPartitionedAccelerationStructureNV */
    { 1000570003, 40 }, /* PartitionedAccelerationStructureInstancesInputNV */
    { 1000570004, 96 }, /* BuildPartitionedAccelerationStructureInfoNV */
    { 1000570005, 24 }, /* PartitionedAccelerationStructureFlagsNV */
    { 1000572000, 24 }, /* PhysicalDeviceDeviceGeneratedCommandsFeaturesEXT */
    { 1000572001, 64 }, /* PhysicalDeviceDeviceGeneratedCommandsPropertiesEXT */
    { 1000572002, 40 }, /* GeneratedCommandsMemoryRequirementsInfoEXT */
    { 1000572003, 32 }, /* IndirectExecutionSetCreateInfoEXT */
    { 1000572004, 96 }, /* GeneratedCommandsInfoEXT */
    { 1000572006, 56 }, /* IndirectCommandsLayoutCreateInfoEXT */
    { 1000572007, 40 }, /* IndirectCommandsLayoutTokenEXT */
    { 1000572008, 32 }, /* WriteIndirectExecutionSetPipelineEXT */
    { 1000572009, 32 }, /* WriteIndirectExecutionSetShaderEXT */
    { 1000572010, 32 }, /* IndirectExecutionSetPipelineInfoEXT */
    { 1000572011, 56 }, /* IndirectExecutionSetShaderInfoEXT */
    { 1000572012, 32 }, /* IndirectExecutionSetShaderLayoutInfoEXT */
    { 1000572013, 24 }, /* GeneratedCommandsPipelineInfoEXT */
    { 1000572014, 32 }, /* GeneratedCommandsShaderInfoEXT */
    { 1000574000, 24 }, /* PhysicalDeviceMaintenance8FeaturesKHR */
    { 1000574002, 32 }, /* MemoryBarrierAccessFlags3KHR */
    { 1000582000, 24 }, /* PhysicalDeviceDepthClampControlFeaturesEXT */
    { 1000582001, 32 }, /* PipelineViewportDepthClampControlCreateInfoEXT */
    { 1000586000, 24 }, /* PhysicalDeviceVideoMaintenance2FeaturesKHR */
    { 1000586001, 32 }, /* VideoDecodeH264InlineSessionParametersInfoKHR */
    { 1000586002, 40 }, /* VideoDecodeH265InlineSessionParametersInfoKHR */
    { 1000590000, 24 }, /* PhysicalDeviceHdrVividFeaturesHUAWEI */
    { 1000590001, 32 }, /* HdrVividDynamicMetadataHUAWEI */
    { 1000593000, 48 }, /* PhysicalDeviceCooperativeMatrix2FeaturesNV */
    { 1000593001, 56 }, /* CooperativeMatrixFlexibleDimensionsPropertiesNV */
    { 1000593002, 32 }, /* PhysicalDeviceCooperativeMatrix2PropertiesNV */
    { 1000596000, 24 }, /* PhysicalDevicePipelineOpacityMicromapFeaturesARM */
    { 1000608000, 24 }, /* PhysicalDeviceVertexAttributeRobustnessFeaturesEXT */
    { 1000613000, 24 }, /* SetPresentConfigNV */
    { 1000613001, 24 }, /* PhysicalDevicePresentMeteringFeaturesNV */
};

size_t vkfw_pnext_size(uint32_t sType)
{
    size_t lo = 0, hi = sizeof(kSizes) / sizeof(kSizes[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kSizes[mid].sType < sType) lo = mid + 1;
        else hi = mid;
    }
    if (lo < sizeof(kSizes) / sizeof(kSizes[0]) && kSizes[lo].sType == sType)
        return (size_t)kSizes[lo].size;
    return 0;
}

/* sTypes of the VkPhysicalDevice*Features* structs, ascending. */
static const uint32_t kFeatureTypes[] = {
    49,
    51,
    53,
    55,
    1000028000,
    1000044003,
    1000050000,
    1000053001,
    1000059000,
    1000063000,
    1000068001,
    1000081001,
    1000082000,
    1000102000,
    1000108000,
    1000110000,
    1000116000,
    1000120000,
    1000138000,
    1000145001,
    1000148000,
    1000150013,
    1000156004,
    1000161001,
    1000164001,
    1000166000,
    1000175000,
    1000180000,
    1000181000,
    1000190002,
    1000201000,
    1000202000,
    1000203000,
    1000204000,
    1000205002,
    1000207000,
    1000209000,
    1000211000,
    1000215000,
    1000218000,
    1000221000,
    1000225002,
    1000226003,
    1000229000,
    1000232000,
    1000234000,
    1000235000,
    1000238000,
    1000240000,
    1000241000,
    1000244000,
    1000248000,
    1000249000,
    1000250000,
    1000251000,
    1000252000,
    1000253000,
    1000254000,
    1000257000,
    1000259000,
    1000260000,
    1000261000,
    1000265000,
    1000267000,
    1000269000,
    1000270000,
    1000272000,
    1000273000,
    1000275000,
    1000276000,
    1000277007,
    1000278000,
    1000280000,
    1000281000,
    1000283000,
    1000284000,
    1000286000,
    1000287002,
    1000292000,
    1000294001,
    1000295000,
    1000297000,
    1000300000,
    1000307003,
    1000314007,
    1000316002,
    1000320000,
    1000321000,
    1000323000,
    1000325000,
    1000326001,
    1000327001,
    1000328000,
    1000330000,
    1000332000,
    1000335000,
    1000336000,
    1000338000,
    1000339000,
    1000340000,
    1000341000,
    1000342000,
    1000347000,
    1000348013,
    1000351000,
    1000352000,
    1000354000,
    1000355000,
    1000356000,
    1000361000,
    1000369001,
    1000370000,
    1000372001,
    1000375000,
    1000376000,
    1000377000,
    1000381000,
    1000382000,
    1000386000,
    1000388000,
    1000391000,
    1000392000,
    1000395000,
    1000396005,
    1000404000,
    1000404002,
    1000411000,
    1000412000,
    1000413000,
    1000416000,
    1000417001,
    1000420000,
    1000421000,
    1000422000,
    1000424000,
    1000425000,
    1000426000,
    1000427000,
    1000428000,
    1000429008,
    1000430000,
    1000434000,
    1000437000,
    1000440000,
    1000451000,
    1000455000,
    1000458000,
    1000462000,
    1000464000,
    1000465000,
    1000466000,
    1000470000,
    1000476000,
    1000481000,
    1000482000,
    1000483000,
    1000484000,
    1000485000,
    1000488000,
    1000490000,
    1000491000,
    1000492000,
    1000495000,
    1000497000,
    1000498000,
    1000499000,
    1000506000,
    1000510000,
    1000515000,
    1000516000,
    1000518000,
    1000519001,
    1000520000,
    1000521000,
    1000524000,
    1000528000,
    1000544000,
    1000545000,
    1000546000,
    1000553009,
    1000555000,
    1000558000,
    1000559000,
    1000562000,
    1000563000,
    1000564000,
    1000568000,
    1000569000,
    1000570000,
    1000572000,
    1000574000,
    1000582000,
    1000586000,
    1000590000,
    1000593000,
    1000596000,
    1000608000,
    1000613001,
};

bool vkfw_pnext_is_features(uint32_t sType)
{
    size_t lo = 0, hi = sizeof(kFeatureTypes) / sizeof(kFeatureTypes[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kFeatureTypes[mid] < sType) lo = mid + 1;
        else hi = mid;
    }
    return lo < sizeof(kFeatureTypes) / sizeof(kFeatureTypes[0]) &&
           kFeatureTypes[lo] == sType;
}

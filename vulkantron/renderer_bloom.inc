// Opt-in arena light scattering, recorded between world and HUD commands.
// Resources share the frame fence and are destroyed on swapchain recreation.
    void prepare_bloom() {
        if (bloom_pipeline) return;
        try {
        faithful_image(bloom_source.image, bloom_source.memory, bloom_source.view,
            faithful_color_format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, extent.width, extent.height);
        auto sampler = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        sampler.minFilter = sampler.magFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        require(vkCreateSampler(device, &sampler, nullptr, &bloom_source.sampler), "Create arena bloom sampler");
        auto allocation = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocation.descriptorPool = faithful_descriptor_pool; allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &faithful_set_layout;
        require(vkAllocateDescriptorSets(device, &allocation, &bloom_source.descriptor), "Allocate arena bloom descriptor");
        const VkDescriptorImageInfo image{bloom_source.sampler,bloom_source.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = bloom_source.descriptor; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        for (int shader = 0; shader < 2; ++shader) {
            const auto words = shader_words(shaders / (shader ? "bloom.frag.spv" : "bloom.vert.spv"));
            auto module = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            module.codeSize = words.size()*4; module.pCode = words.data();
            require(vkCreateShaderModule(device, &module, nullptr,
                shader ? &bloom_fragment_shader : &bloom_vertex_shader), "Create arena bloom shader");
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i=0; i<2; ++i) {
            stages[i] = info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = i ? bloom_fragment_shader : bloom_vertex_shader;
            stages[i].pName = "main";
        }
        auto vertex = info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        auto viewport = info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport.viewportCount = viewport.scissorCount = 1;
        auto raster = info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
        auto ms = info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        auto depth = info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        auto blend = info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        auto dynamic = info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
        auto rendering = info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &faithful_color_format;
        rendering.depthAttachmentFormat = rendering.stencilAttachmentFormat = faithful_depth_format;
        auto create = info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        create.pNext = &rendering; create.stageCount = 2; create.pStages = stages;
        create.pVertexInputState = &vertex; create.pInputAssemblyState = &assembly;
        create.pViewportState = &viewport; create.pRasterizationState = &raster;
        create.pMultisampleState = &ms; create.pDepthStencilState = &depth;
        create.pColorBlendState = &blend; create.pDynamicState = &dynamic; create.layout = faithful_layout;
        require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create, nullptr, &bloom_pipeline), "Create arena bloom pipeline");
        } catch (...) {
            // A screenshot request may report a load failure and then retry
            // the frame on swap. Never overwrite partial Vulkan ownership.
            destroy_bloom();
            throw;
        }
    }

    void draw_bloom(const BloomCommand& bloom, VkRenderingInfo& rendering,
                    VkRenderingAttachmentInfo& color, VkRenderingAttachmentInfo& depth) {
        const auto rect = faithful_rectangle(bloom.viewport);
        if (!rect.extent.width || !rect.extent.height || bloom.strength <= 0) return;
        vkCmdEndRendering(command);
        image_barrier(faithful_color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        image_barrier(bloom_source.image, VK_IMAGE_ASPECT_COLOR_BIT,
            bloom_used ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bloom_used ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
            bloom_used ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.srcOffset = copy.dstOffset = {rect.offset.x,rect.offset.y,0};
        copy.extent = {rect.extent.width,rect.extent.height,1};
        vkCmdCopyImage(command, faithful_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            bloom_source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        image_barrier(bloom_source.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        image_barrier(faithful_color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        image_barrier(faithful_depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        color.loadOp = depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        vkCmdBeginRendering(command, &rendering);
        const VkViewport viewport{static_cast<float>(rect.offset.x),static_cast<float>(rect.offset.y),
            static_cast<float>(rect.extent.width),static_cast<float>(rect.extent.height),0,1};
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_pipeline);
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &rect);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, faithful_layout, 0, 1, &bloom_source.descriptor, 0, nullptr);
        const float iw = 1.0f/extent.width, ih = 1.0f/extent.height;
        const float push[8] = {(rect.offset.x+0.5f)*iw,(rect.offset.y+0.5f)*ih,
            (rect.offset.x+rect.extent.width-0.5f)*iw,(rect.offset.y+rect.extent.height-0.5f)*ih,
            iw,ih,std::clamp(rect.extent.height/180.0f,1.5f,8.0f),bloom.strength};
        vkCmdPushConstants(command, faithful_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
        vkCmdDraw(command, 3, 1, 0, 0);
        bloom_used = true;
    }

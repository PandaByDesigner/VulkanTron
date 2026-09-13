// Private Renderer::Impl methods; included by renderer.cpp inside the class.
// This backend consumes game-specific recordings, without an OpenGL runtime.

    void destroy_faithful_texture(FaithfulTexture& texture) noexcept {
        if (texture.descriptor && faithful_descriptor_pool)
            vkFreeDescriptorSets(device, faithful_descriptor_pool, 1, &texture.descriptor);
        if (texture.sampler) vkDestroySampler(device, texture.sampler, nullptr);
        if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
        if (texture.image) vkDestroyImage(device, texture.image, nullptr);
        if (texture.memory) vkFreeMemory(device, texture.memory, nullptr);
        texture = {};
    }
    void destroy_faithful_target() noexcept {
        for (auto& item : faithful_pipelines) vkDestroyPipeline(device, item.second, nullptr);
        faithful_pipelines.clear();
        if (faithful_color_view) vkDestroyImageView(device, faithful_color_view, nullptr);
        if (faithful_depth_view) vkDestroyImageView(device, faithful_depth_view, nullptr);
        if (faithful_color) vkDestroyImage(device, faithful_color, nullptr);
        if (faithful_depth) vkDestroyImage(device, faithful_depth, nullptr);
        if (faithful_color_memory) vkFreeMemory(device, faithful_color_memory, nullptr);
        if (faithful_depth_memory) vkFreeMemory(device, faithful_depth_memory, nullptr);
        faithful_color_view = faithful_depth_view = VK_NULL_HANDLE;
        faithful_color = faithful_depth = VK_NULL_HANDLE;
        faithful_color_memory = faithful_depth_memory = VK_NULL_HANDLE;
        faithful_used = false;
    }
    void destroy_faithful() noexcept {
        destroy_faithful_target();
        for (auto& upload : faithful_uploads) destroy_buffer(upload);
        faithful_uploads.clear();
        for (auto& item : faithful_textures) destroy_faithful_texture(item.second);
        faithful_textures.clear();
        if (faithful_layout) vkDestroyPipelineLayout(device, faithful_layout, nullptr);
        if (faithful_vertex_shader) vkDestroyShaderModule(device, faithful_vertex_shader, nullptr);
        if (faithful_fragment_shader) vkDestroyShaderModule(device, faithful_fragment_shader, nullptr);
        if (faithful_descriptor_pool) vkDestroyDescriptorPool(device, faithful_descriptor_pool, nullptr);
        if (faithful_set_layout) vkDestroyDescriptorSetLayout(device, faithful_set_layout, nullptr);
        faithful_layout = VK_NULL_HANDLE;
        faithful_vertex_shader = faithful_fragment_shader = VK_NULL_HANDLE;
        faithful_descriptor_pool = VK_NULL_HANDLE;
        faithful_set_layout = VK_NULL_HANDLE;
    }
    void faithful_image(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                        VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                        std::uint32_t width, std::uint32_t height, std::uint32_t mips = 1) {
        auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        create.imageType = VK_IMAGE_TYPE_2D; create.format = format;
        create.extent = {width, height, 1}; create.mipLevels = mips; create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT; create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = usage; create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        require(vkCreateImage(device, &create, nullptr, &image), "Create faithful image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        require(vkAllocateMemory(device, &allocation, nullptr, &memory), "Allocate faithful image memory");
        require(vkBindImageMemory(device, image, memory, 0), "Bind faithful image memory");
        auto view_info = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view_info.image = image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; view_info.format = format;
        view_info.subresourceRange = {aspect, 0, mips, 0, 1};
        require(vkCreateImageView(device, &view_info, nullptr, &view), "Create faithful image view");
    }
    void prepare_faithful() {
        if (faithful_depth_format == VK_FORMAT_UNDEFINED)
            throw std::runtime_error("Faithful renderer requires a depth-stencil attachment format");
        if (!faithful_set_layout) {
            std::fprintf(stderr, "[Vulkan] faithful depth volume: %s\n",
                native_clip_depth ? "native GL [-1,1]" : force_standard_depth ?
                "standard Vulkan [0,1] conversion (VULKANTRON_NATIVE_CLIP_DEPTH=0)" :
                "standard Vulkan [0,1] conversion (native depth feature unavailable)");
            const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            auto create = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
            create.bindingCount = 1; create.pBindings = &binding;
            require(vkCreateDescriptorSetLayout(device, &create, nullptr, &faithful_set_layout), "Create faithful texture layout");
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512};
            auto pool_info = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 512; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &size;
            require(vkCreateDescriptorPool(device, &pool_info, nullptr, &faithful_descriptor_pool), "Create faithful texture pool");
            const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
            auto layout = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
            layout.setLayoutCount = 1; layout.pSetLayouts = &faithful_set_layout;
            layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
            require(vkCreatePipelineLayout(device, &layout, nullptr, &faithful_layout), "Create faithful pipeline layout");
            for (int shader = 0; shader < 2; ++shader) {
                const auto words = shader_words(shaders / (shader ? "faithful.frag.spv" : "faithful.vert.spv"));
                auto module = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
                module.codeSize = words.size() * 4; module.pCode = words.data();
                require(vkCreateShaderModule(device, &module, nullptr,
                    shader ? &faithful_fragment_shader : &faithful_vertex_shader), "Create faithful shader");
            }
        }
        if (!faithful_color) {
            faithful_color_format = (color_format == VK_FORMAT_B8G8R8A8_SRGB || color_format == VK_FORMAT_B8G8R8A8_UNORM) ?
                                     VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
            faithful_image(faithful_color, faithful_color_memory, faithful_color_view, faithful_color_format,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, extent.width, extent.height);
            faithful_image(faithful_depth, faithful_depth_memory, faithful_depth_view, faithful_depth_format,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, extent.width, extent.height);
        }
    }
    static VkCompareOp faithful_compare(std::uint32_t value) {
        if (value < 0x0200 || value > 0x0207) throw std::runtime_error("Unsupported faithful comparison function");
        return static_cast<VkCompareOp>(value - 0x0200);
    }
    static VkStencilOp faithful_stencil(std::uint32_t value) {
        switch (value) {
        case 0: return VK_STENCIL_OP_ZERO;
        case 0x1e00: return VK_STENCIL_OP_KEEP;
        case 0x1e01: return VK_STENCIL_OP_REPLACE;
        case 0x1e02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 0x1e03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 0x150a: return VK_STENCIL_OP_INVERT;
        case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: throw std::runtime_error("Unsupported faithful stencil operation");
        }
    }
    static VkBlendFactor faithful_blend(std::uint32_t value) {
        switch (value) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x0306: return VK_BLEND_FACTOR_DST_COLOR;
        case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x0308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: throw std::runtime_error("Unsupported faithful blend factor");
        }
    }
    static VkSamplerAddressMode faithful_wrap(std::uint32_t value) {
        switch (value) {
        case 0x2901: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 0x8370: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 0x812f: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 0x2900: case 0x812d: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: throw std::runtime_error("Unsupported faithful texture wrapping");
        }
    }
    VkSampler faithful_sampler(const Texture& texture) {
        auto create = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        if (texture.mag_filter != 0x2600 && texture.mag_filter != 0x2601)
            throw std::runtime_error("Unsupported faithful texture magnification filter");
        create.magFilter = texture.mag_filter == 0x2600 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        switch (texture.min_filter) {
        case 0x2600: create.minFilter = VK_FILTER_NEAREST; break;
        case 0x2601: create.minFilter = VK_FILTER_LINEAR; break;
        case 0x2700: create.minFilter = VK_FILTER_NEAREST; create.maxLod = static_cast<float>(texture.levels.size()-1); break;
        case 0x2701: create.minFilter = VK_FILTER_LINEAR; create.maxLod = static_cast<float>(texture.levels.size()-1); break;
        case 0x2702: create.minFilter = VK_FILTER_NEAREST; create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; create.maxLod = static_cast<float>(texture.levels.size()-1); break;
        case 0x2703: create.minFilter = VK_FILTER_LINEAR; create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; create.maxLod = static_cast<float>(texture.levels.size()-1); break;
        default: throw std::runtime_error("Unsupported faithful texture minification filter");
        }
        create.addressModeU = faithful_wrap(texture.wrap_s);
        create.addressModeV = faithful_wrap(texture.wrap_t);
        create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (texture.border_color == std::array<float,4>{0,0,0,0}) create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else if (texture.border_color == std::array<float,4>{0,0,0,1}) create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if (texture.border_color == std::array<float,4>{1,1,1,1}) create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else throw std::runtime_error("Custom texture border colors require an additional Vulkan feature; original game uses black");
        if (!std::isfinite(texture.anisotropy) || texture.anisotropy < 1)
            throw std::runtime_error("Invalid faithful texture anisotropy");
        if (texture.anisotropy > 1) {
            if (!enabled_features.samplerAnisotropy) throw std::runtime_error("Device lacks requested anisotropic filtering");
            create.anisotropyEnable = VK_TRUE;
            create.maxAnisotropy = std::min(texture.anisotropy, physical_properties.limits.maxSamplerAnisotropy);
        }
        VkSampler result = VK_NULL_HANDLE;
        require(vkCreateSampler(device, &create, nullptr, &result), "Create faithful sampler");
        return result;
    }
    FaithfulTexture& upload_faithful_texture(const std::shared_ptr<const Texture>& source) {
        const TextureKey key{source->id, source->revision};
        if (auto found = faithful_textures.find(key); found != faithful_textures.end()) return found->second;
        if (faithful_textures.size() >= 512) throw std::runtime_error("Faithful texture snapshot limit exceeded");
        if (source->levels.empty() || source->levels.size() > 32) throw std::runtime_error("Missing or invalid faithful mip chain");
        std::uint32_t width = source->levels[0].width, height = source->levels[0].height;
        std::uint32_t max_levels = 1;
        for (auto size = std::max(width,height); size > 1; size /= 2) ++max_levels;
        if (source->levels.size() > max_levels) throw std::runtime_error("Too many faithful mip levels");
        VkDeviceSize bytes = 0;
        std::vector<VkBufferImageCopy> regions;
        for (std::size_t level = 0; level < source->levels.size(); ++level) {
            const auto& pixels = source->levels[level];
            const auto level_bytes = static_cast<VkDeviceSize>(width) * height * 4;
            if (!width || !height || width > physical_properties.limits.maxImageDimension2D ||
                height > physical_properties.limits.maxImageDimension2D || pixels.width != width || pixels.height != height ||
                level_bytes > max_readback_bytes || pixels.rgba.size() != level_bytes || bytes > max_readback_bytes - level_bytes)
                throw std::runtime_error("Malformed or oversized faithful texture mip chain");
            VkBufferImageCopy copy{};
            copy.bufferOffset = bytes;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(level), 0, 1};
            copy.imageExtent = {width, height, 1};
            regions.push_back(copy);
            bytes += level_bytes;
            width = std::max(1u, width/2); height = std::max(1u, height/2);
        }
        VkDeviceSize total = bytes;
        for (const auto& entry : faithful_textures) total += entry.second.bytes;
        if (total > 768ull * 1024 * 1024) throw std::runtime_error("Faithful texture storage exceeds 768 MiB budget");
        FaithfulTexture result;
        result.source = source; result.bytes = bytes;
        Buffer upload;
        try {
            faithful_image(result.image, result.memory, result.view, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                source->levels[0].width, source->levels[0].height, static_cast<std::uint32_t>(source->levels.size()));
            result.sampler = faithful_sampler(*source);
            auto allocation = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            allocation.descriptorPool = faithful_descriptor_pool;
            allocation.descriptorSetCount = 1; allocation.pSetLayouts = &faithful_set_layout;
            require(vkAllocateDescriptorSets(device, &allocation, &result.descriptor), "Allocate faithful texture descriptor");
            const VkDescriptorImageInfo image_info{result.sampler, result.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            write.dstSet = result.descriptor; write.dstBinding = 0; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            ensure_buffer(upload, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            for (std::size_t level = 0; level < source->levels.size(); ++level)
                std::memcpy(static_cast<unsigned char*>(upload.mapped) + regions[level].bufferOffset,
                            source->levels[level].rgba.data(), source->levels[level].rgba.size());
            host_memory(upload, false);
            image_barrier(result.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, static_cast<std::uint32_t>(source->levels.size()));
            vkCmdCopyBufferToImage(command, upload.handle, result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<std::uint32_t>(regions.size()), regions.data());
            image_barrier(result.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                static_cast<std::uint32_t>(source->levels.size()));
            faithful_uploads.push_back(upload);
            upload = {};
            auto inserted = faithful_textures.emplace(key, std::move(result));
            return inserted.first->second;
        } catch (...) { destroy_buffer(upload); destroy_faithful_texture(result); throw; }
    }
    VkRect2D faithful_rectangle(const std::array<int,4>& rectangle) const {
        const auto left = std::clamp<std::int64_t>(rectangle[0], 0, extent.width);
        const auto right = std::clamp<std::int64_t>(static_cast<std::int64_t>(rectangle[0]) + rectangle[2], left, extent.width);
        const auto bottom = std::clamp<std::int64_t>(rectangle[1], 0, extent.height);
        const auto top = std::clamp<std::int64_t>(static_cast<std::int64_t>(rectangle[1]) + rectangle[3], bottom, extent.height);
        return {{static_cast<std::int32_t>(left), static_cast<std::int32_t>(extent.height-top)},
                {static_cast<std::uint32_t>(right-left), static_cast<std::uint32_t>(top-bottom)}};
    }
    VkPipeline faithful_pipeline(const DrawBatch& batch) {
        const auto& state = batch.state;
        const bool draw_lines = batch.primitive == Primitive::LineList || state.polygon_mode == 0x1b01;
        const bool smooth = draw_lines && state.line_smooth;
        const std::uint32_t flags = state.depth_test | (state.depth_write << 1) | (state.cull << 2) |
            (state.blend << 3) | (state.stencil << 4) | (state.polygon_offset << 5) | (smooth << 6);
        const std::array<std::uint32_t,16> key = {static_cast<std::uint32_t>(batch.primitive), flags,
            state.depth_func, state.cull_face, state.front_face, state.blend_src, state.blend_dst,
            state.stencil_func, state.stencil_fail, state.stencil_depth_fail, state.stencil_pass,
            state.polygon_mode, 0, 0, 0, 0};
        if (auto found = faithful_pipelines.find(key); found != faithful_pipelines.end()) return found->second;
        if (faithful_pipelines.size() >= 512) throw std::runtime_error("Faithful pipeline limit exceeded");
        auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        switch (batch.primitive) {
        case Primitive::TriangleList: assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case Primitive::LineList: assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case Primitive::PointList: assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
        default: throw std::runtime_error("Unsupported faithful primitive");
        }
        auto raster = info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        switch (state.polygon_mode) {
        case 0x1b02: raster.polygonMode = VK_POLYGON_MODE_FILL; break;
        case 0x1b01: raster.polygonMode = VK_POLYGON_MODE_LINE; break;
        case 0x1b00: raster.polygonMode = VK_POLYGON_MODE_POINT; break;
        default: throw std::runtime_error("Unsupported faithful polygon mode");
        }
        if (raster.polygonMode != VK_POLYGON_MODE_FILL && !enabled_features.fillModeNonSolid)
            throw std::runtime_error("Device lacks requested non-solid polygon mode");
        if (state.cull) {
            switch (state.cull_face) {
            case 0x0404: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
            case 0x0405: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
            case 0x0408: raster.cullMode = VK_CULL_MODE_FRONT_AND_BACK; break;
            default: throw std::runtime_error("Unsupported faithful cull face");
            }
        }
        if (state.front_face != 0x0900 && state.front_face != 0x0901)
            throw std::runtime_error("Unsupported faithful front-face convention");
        // The adapter negates clip Y for Vulkan's downward framebuffer Y.
        // Vulkan defines facing using that framebuffer convention, so after
        // this conversion the original GL front-face token is preserved.
        raster.frontFace = state.front_face == 0x0901 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1;
        raster.depthBiasEnable = state.polygon_offset && state.polygon_mode == 0x1b02;
        auto lines = info<VkPipelineRasterizationLineStateCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);
        if (smooth && !smooth_lines) throw std::runtime_error("Device lacks requested smooth line rasterization");
        if (line_rasterization) {
            lines.lineRasterizationMode = smooth ? VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT :
                bresenham_lines ? VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT : VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT;
            raster.pNext = &lines;
        }
        auto depth = info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        depth.depthTestEnable = state.depth_test;
        depth.depthWriteEnable = state.depth_test && state.depth_write;
        depth.depthCompareOp = faithful_compare(state.depth_func);
        depth.stencilTestEnable = state.stencil;
        depth.front.failOp = faithful_stencil(state.stencil_fail);
        depth.front.passOp = faithful_stencil(state.stencil_pass);
        depth.front.depthFailOp = faithful_stencil(state.stencil_depth_fail);
        depth.front.compareOp = faithful_compare(state.stencil_func);
        depth.back = depth.front;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 15;
        attachment.blendEnable = state.blend;
        attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = faithful_blend(state.blend_src);
        attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = faithful_blend(state.blend_dst);
        // GL_SRC_ALPHA_SATURATE has an alpha factor of one.
        if (state.blend_src == 0x0308) attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        auto blend = info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        const VkVertexInputBindingDescription binding{0, sizeof(ClipVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[] = {
            {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(ClipVertex,position)},
            {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(ClipVertex,front_color)},
            {2,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(ClipVertex,back_color)},
            {3,0,VK_FORMAT_R32G32_SFLOAT,offsetof(ClipVertex,uv)},
            {4,0,VK_FORMAT_R32_SFLOAT,offsetof(ClipVertex,fog_distance)},
            {5,0,VK_FORMAT_R32_SFLOAT,offsetof(ClipVertex,gl_clip_z)}};
        auto input = info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 6; input.pVertexAttributeDescriptions = attributes;
        auto viewport = info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport.viewportCount = viewport.scissorCount = 1;
        auto clip_control = info<VkPipelineViewportDepthClipControlCreateInfoEXT>(
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT);
        clip_control.negativeOneToOne = native_clip_depth;
        viewport.pNext = native_clip_depth ? &clip_control : nullptr;
        auto multisample = info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        auto dynamic = info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = 7; dynamic.pDynamicStates = dynamic_states;
        VkPipelineShaderStageCreateInfo stages[2] = {
            info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO),
            info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = faithful_vertex_shader; stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = faithful_fragment_shader; stages[1].pName = "main";
        auto rendering = info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &faithful_color_format;
        rendering.depthAttachmentFormat = rendering.stencilAttachmentFormat = faithful_depth_format;
        auto create = info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        create.pNext = &rendering; create.stageCount = 2; create.pStages = stages;
        create.pVertexInputState = &input; create.pInputAssemblyState = &assembly;
        create.pViewportState = &viewport; create.pRasterizationState = &raster;
        create.pMultisampleState = &multisample; create.pDepthStencilState = &depth;
        create.pColorBlendState = &blend; create.pDynamicState = &dynamic; create.layout = faithful_layout;
        VkPipeline result = VK_NULL_HANDLE;
        require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create, nullptr, &result), "Create faithful pipeline");
        try { faithful_pipelines.emplace(key, result); }
        catch (...) { vkDestroyPipeline(device, result, nullptr); throw; }
        return result;
    }
    std::size_t validate_faithful(const FaithfulFrame& frame) const {
        if (frame.commands.size() > 65536 || frame.textures.size() > 511)
            throw std::runtime_error("Faithful frame command/texture limit exceeded");
        std::size_t count = 0;
        for (const auto& entry : frame.commands) {
            if (const auto* clear = std::get_if<ClearCommand>(&entry)) {
                for (float value : clear->color) if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite faithful clear color");
                if (!std::isfinite(clear->depth)) throw std::runtime_error("Nonfinite faithful clear depth");
                // VkClearAttachments writes all stencil bits. A draw-based
                // masked clear would be needed for non-production masks.
                if ((clear->mask & 0x0400) && (clear->stencil_write_mask & 255) != 0 &&
                    (clear->stencil_write_mask & 255) != 255)
                    throw std::runtime_error("Partial stencil clear mask is not used by the classic game and is unsupported");
                continue;
            }
            const auto& draw = std::get<DrawBatch>(entry);
            std::size_t multiple = 1;
            switch (draw.primitive) {
            case Primitive::TriangleList: multiple = 3; break;
            case Primitive::LineList: multiple = 2; break;
            case Primitive::PointList: break;
            default: throw std::runtime_error("Invalid faithful primitive");
            }
            if (draw.vertices.size() % multiple || draw.vertices.size() > max_vertices-count)
                throw std::runtime_error("Malformed or oversized faithful vertex list");
            count += draw.vertices.size();
            for (const auto& vertex : draw.vertices) {
                for (float value : vertex.position) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful clip vertex");
                for (float value : vertex.front_color) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful front color");
                for (float value : vertex.back_color) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful back color");
                for (float value : vertex.uv) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful texture coordinates");
                if (!std::isfinite(vertex.fog_distance)) throw std::runtime_error("Nonfinite faithful fog coordinate");
                if (!std::isfinite(vertex.gl_clip_z)) throw std::runtime_error("Nonfinite original GL clip depth");
            }
            const auto& state = draw.state;
            for (float value : {state.line_width, state.point_size, state.polygon_offset_factor,
                                state.polygon_offset_units, state.fog_start, state.fog_end, state.fog_density})
                if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful draw state");
            for (float value : state.fog_color) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite faithful fog color");
            if (state.line_width <= 0 || state.line_width < physical_properties.limits.lineWidthRange[0] ||
                state.line_width > physical_properties.limits.lineWidthRange[1] ||
                (state.line_width != 1 && !enabled_features.wideLines))
                throw std::runtime_error("Unsupported faithful line width");
            if (state.point_size <= 0 || state.point_size < physical_properties.limits.pointSizeRange[0] ||
                state.point_size > physical_properties.limits.pointSizeRange[1] ||
                (state.point_size != 1 && !enabled_features.largePoints))
                throw std::runtime_error("Unsupported faithful point size");
            if (state.viewport[2] < 0 || state.viewport[3] < 0 ||
                static_cast<std::uint32_t>(state.viewport[2]) > physical_properties.limits.maxViewportDimensions[0] ||
                static_cast<std::uint32_t>(state.viewport[3]) > physical_properties.limits.maxViewportDimensions[1])
                throw std::runtime_error("Invalid faithful viewport dimensions");
            if (state.texture_enabled && !state.texture) throw std::runtime_error("Missing enabled faithful texture snapshot");
            if (state.texture_env != 0x2100 && state.texture_env != 0x2101 && state.texture_env != 0x1e01)
                throw std::runtime_error("Unsupported faithful texture environment");
            if (state.fog && state.fog_mode != 0x2601 && state.fog_mode != 0x0800 && state.fog_mode != 0x0801)
                throw std::runtime_error("Unsupported faithful fog mode");
        }
        return count;
    }
    bool draw_faithful(const FaithfulFrame& frame, std::vector<std::uint8_t>* rgb) {
        if (shutdown_complete) throw std::runtime_error("Cannot draw after renderer shutdown");
        if (acquire_unconsumed) throw std::runtime_error("Previous frame failed after acquisition; recreate renderer");
        if (rgb) rgb->clear();
        const auto count = validate_faithful(frame);
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            throw std::runtime_error(std::string("Query faithful drawable: ") + SDL_GetError());
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        if (!faithful_mode) { faithful_mode = true; dirty = true; }
        if ((dirty || width != drawable_width || height != drawable_height) && !recreate(width, height)) return false;
        if (submission_pending) {
            require(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for faithful frame");
            submission_pending = false;
        }
        for (auto& upload : faithful_uploads) destroy_buffer(upload);
        faithful_uploads.clear();
        prepare_faithful();
        static const std::shared_ptr<const Texture> white = [] {
            auto result = std::make_shared<Texture>();
            result->id = UINT32_MAX; result->revision = UINT64_MAX;
            result->min_filter = result->mag_filter = 0x2600;
            result->levels.push_back({1,1,{255,255,255,255}});
            return result;
        }();
        std::map<TextureKey,std::shared_ptr<const Texture>> required;
        required.emplace(TextureKey{white->id,white->revision}, white);
        std::set<TextureKey> live;
        for (const auto& texture : frame.textures) if (texture)
            live.emplace(texture->id,texture->revision);
        for (const auto& entry : frame.commands) if (const auto* draw = std::get_if<DrawBatch>(&entry)) {
            if (draw->state.texture_enabled && draw->state.texture) {
                const auto& texture = draw->state.texture;
                required.emplace(TextureKey{texture->id,texture->revision}, texture);
            }
            // Build pipelines before acquiring, so an unsupported state cannot
            // leave the acquire semaphore outstanding during normal retries.
            faithful_pipeline(*draw);
        }
        for (auto it = faithful_textures.begin(); it != faithful_textures.end();) {
            if (!required.count(it->first) && !live.count(it->first)) { destroy_faithful_texture(it->second); it = faithful_textures.erase(it); }
            else ++it;
        }
        if (count) {
            ensure_buffer(vertices, static_cast<VkDeviceSize>(count)*sizeof(ClipVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            auto* output = static_cast<std::uint8_t*>(vertices.mapped);
            for (const auto& entry : frame.commands) if (const auto* draw = std::get_if<DrawBatch>(&entry)) {
                const auto bytes = draw->vertices.size()*sizeof(ClipVertex);
                if (bytes) std::memcpy(output, draw->vertices.data(), bytes);
                output += bytes;
            }
            host_memory(vertices, false);
        }
        if (rgb) ensure_buffer(readback, static_cast<VkDeviceSize>(extent.width)*extent.height*4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        std::uint32_t index = 0;
        require(vkResetFences(device, 1, &acquire_fence), "Reset faithful acquire fence");
        const auto acquired_result = vkAcquireNextImageKHR(device, swapchain, 1000000000ull, acquired, acquire_fence, &index);
        if (acquired_result == VK_ERROR_OUT_OF_DATE_KHR) { dirty = true; return false; }
        if (acquired_result == VK_TIMEOUT || acquired_result == VK_NOT_READY) return false;
        if (acquired_result != VK_SUBOPTIMAL_KHR) require(acquired_result, "Acquire faithful swapchain image");
        acquire_unconsumed = true;
        require(vkWaitForFences(device, 1, &acquire_fence, VK_TRUE, UINT64_MAX), "Wait for faithful image acquisition");
        auto& image = images.at(index);
        if (maintenance) {
            if (image.present_pending) {
                require(vkWaitForFences(device, 1, &image.present_fence, VK_TRUE, UINT64_MAX), "Wait for faithful presentation");
                image.present_pending = false;
            }
            require(vkResetFences(device, 1, &image.present_fence), "Reset faithful presentation fence");
        }
        require(vkResetCommandBuffer(command, 0), "Reset faithful commands");
        auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(command, &begin), "Begin faithful commands");
        for (const auto& texture : required) upload_faithful_texture(texture.second);
        image_barrier(faithful_color, VK_IMAGE_ASPECT_COLOR_BIT,
            faithful_used ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            faithful_used ? VK_PIPELINE_STAGE_2_TRANSFER_BIT : VK_PIPELINE_STAGE_2_NONE,
            faithful_used ? VK_ACCESS_2_TRANSFER_READ_BIT : 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        image_barrier(faithful_depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            faithful_used ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            faithful_used ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_2_NONE,
            faithful_used ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        auto color = info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        color.imageView = faithful_color_view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = faithful_used ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        auto depth = info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth.imageView = faithful_depth_view; depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = color.loadOp; depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = {1,0};
        auto rendering = info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.extent = extent; rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = rendering.pStencilAttachment = &depth;
        vkCmdBeginRendering(command, &rendering);
        if (count) {
            const VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &zero);
        }
        std::uint32_t first_vertex = 0;
        for (const auto& entry : frame.commands) {
            if (const auto* clear = std::get_if<ClearCommand>(&entry)) {
                const VkRect2D rectangle = clear->scissor_test ? faithful_rectangle(clear->scissor) : VkRect2D{{0,0},extent};
                if (!rectangle.extent.width || !rectangle.extent.height) continue;
                VkClearAttachment attachments[2]{};
                std::uint32_t attachment_count = 0;
                if (clear->mask & 0x4000) {
                    auto& attachment = attachments[attachment_count++];
                    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    for (int c = 0; c < 4; ++c) attachment.clearValue.color.float32[c] = std::clamp(clear->color[c],0.0f,1.0f);
                }
                VkImageAspectFlags aspects = 0;
                if ((clear->mask & 0x0100) && clear->depth_write) aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
                if ((clear->mask & 0x0400) && (clear->stencil_write_mask & 255)) aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
                if (aspects) {
                    auto& attachment = attachments[attachment_count++];
                    attachment.aspectMask = aspects;
                    attachment.clearValue.depthStencil = {std::clamp(clear->depth,0.0f,1.0f),clear->stencil & 255};
                }
                const VkClearRect area{rectangle,0,1};
                if (attachment_count) vkCmdClearAttachments(command, attachment_count, attachments, 1, &area);
                continue;
            }
            const auto& batch = std::get<DrawBatch>(entry);
            const auto& state = batch.state;
            const auto vertex_count = static_cast<std::uint32_t>(batch.vertices.size());
            const auto start = first_vertex;
            first_vertex += vertex_count;
            const VkRect2D scissor = state.scissor_test ? faithful_rectangle(state.scissor) : VkRect2D{{0,0},extent};
            if (!vertex_count || !state.viewport[2] || !state.viewport[3] || !scissor.extent.width || !scissor.extent.height) continue;
            const double vx = state.viewport[0];
            const double vy = static_cast<double>(extent.height) - state.viewport[1] - state.viewport[3];
            const auto* limits = physical_properties.limits.viewportBoundsRange;
            if (vx < limits[0] || vy < limits[0] || vx+state.viewport[2] > limits[1] || vy+state.viewport[3] > limits[1])
                throw std::runtime_error("Faithful viewport outside Vulkan coordinate bounds");
            const VkViewport viewport{static_cast<float>(vx),static_cast<float>(vy),
                static_cast<float>(state.viewport[2]),static_cast<float>(state.viewport[3]),0,1};
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, faithful_pipeline(batch));
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdSetLineWidth(command, state.line_width);
            vkCmdSetDepthBias(command, state.polygon_offset_units, 0, state.polygon_offset_factor);
            vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencil_read_mask);
            vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencil_write_mask);
            vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, static_cast<std::uint32_t>(std::clamp(state.stencil_ref,0,255)));
            const auto& source = state.texture_enabled ? state.texture : white;
            const auto& texture = faithful_textures.at(TextureKey{source->id,source->revision});
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, faithful_layout, 0, 1, &texture.descriptor, 0, nullptr);
            struct Push {
                std::array<float,4> fog_color, fog_params;
                std::array<std::uint32_t,4> modes, wraps;
            } push{state.fog_color,{state.fog_start,state.fog_end,state.fog_density,state.point_size},
                {state.texture_enabled,state.texture_env,source->base_format,state.fog ? state.fog_mode : 0},
                {source->wrap_s,source->wrap_t,native_clip_depth,0}};
            static_assert(sizeof(Push) == 64);
            vkCmdPushConstants(command, faithful_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(command, vertex_count, 1, start, 0);
        }
        vkCmdEndRendering(command);
        image_barrier(faithful_color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        // Match the acquisition wait stage in this first image transition. Its
        // execution chain protects the previous presentation's read of image.
        image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.extent = {extent.width,extent.height,1};
        // Copy, never blit: matching UNORM/sRGB channel layouts retain original
        // GL numeric colors without adding gamma conversion to fixed blending.
        vkCmdCopyImage(command, faithful_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        if (rgb) {
            VkBufferImageCopy capture{};
            capture.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            capture.imageExtent = {extent.width,extent.height,1};
            vkCmdCopyImageToBuffer(command, faithful_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &capture);
            auto barrier = info<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = readback.handle; barrier.size = VK_WHOLE_SIZE;
            auto dependency = info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(command, &dependency);
        }
        image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0);
        require(vkEndCommandBuffer(command), "End faithful commands");
        auto wait = info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        wait.semaphore = acquired; wait.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        auto signal = info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signal.semaphore = image.finished; signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        auto commands = info<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        commands.commandBuffer = command;
        auto submit = info<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submit.waitSemaphoreInfoCount = 1; submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &commands;
        submit.signalSemaphoreInfoCount = 1; submit.pSignalSemaphoreInfos = &signal;
        require(vkResetFences(device, 1, &submitted), "Reset faithful submit fence");
        require(vkQueueSubmit2(graphics, 1, &submit, submitted), "Submit faithful frame");
        submission_pending = true; acquire_unconsumed = false; faithful_used = true;
        auto present_fence = info<VkSwapchainPresentFenceInfoEXT>(VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT);
        present_fence.swapchainCount = 1; present_fence.pFences = &image.present_fence;
        auto presentation = info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        presentation.pNext = maintenance ? &present_fence : nullptr;
        presentation.waitSemaphoreCount = 1; presentation.pWaitSemaphores = &image.finished;
        presentation.swapchainCount = 1; presentation.pSwapchains = &swapchain; presentation.pImageIndices = &index;
        const auto result = vkQueuePresentKHR(present, &presentation);
        if (maintenance && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR))
            image.present_pending = true;
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) require(result, "Present faithful frame");
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquired_result == VK_SUBOPTIMAL_KHR) dirty = true;
        if (rgb) {
            require(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for faithful readback");
            submission_pending = false;
            host_memory(readback, true);
            rgb->resize(static_cast<std::size_t>(extent.width)*extent.height*3);
            const auto* input = static_cast<const std::uint8_t*>(readback.mapped);
            const bool bgra = faithful_color_format == VK_FORMAT_B8G8R8A8_UNORM;
            for (std::uint32_t y = 0; y < extent.height; ++y) for (std::uint32_t x = 0; x < extent.width; ++x) {
                const std::size_t source = (static_cast<std::size_t>(extent.height-1-y)*extent.width+x)*4;
                const std::size_t destination = (static_cast<std::size_t>(y)*extent.width+x)*3;
                (*rgb)[destination] = input[source+(bgra ? 2 : 0)];
                (*rgb)[destination+1] = input[source+1];
                (*rgb)[destination+2] = input[source+(bgra ? 0 : 2)];
            }
        }
        ++statistics.frames;
        statistics.validation_errors = validation_errors.load(std::memory_order_relaxed);
        return true;
    }

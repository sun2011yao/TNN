// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "tnn/device/opencl/acc/opencl_cpu_adapter_acc.h"

#include "tnn/core/abstract_device.h"
#include "tnn/utils/blob_converter.h"
#include "tnn/utils/data_format_converter.h"

namespace TNN_NS {

OpenCLCpuAdapterAcc::OpenCLCpuAdapterAcc(LayerType impl_layer_type) {
    impl_layer_type_ = impl_layer_type;
    DeviceType device_list[2] = {DEVICE_ARM, DEVICE_NAIVE};
    for(auto device_type : device_list) {
        auto device = GetDevice(device_type);
        if(device != NULL) {
            auto acc = device->CreateLayerAcc(impl_layer_type_);
            if(acc != NULL) {
                cpu_adapter_acc_ = acc;
                impl_device_type_ = device_type;
                impl_device_context_ = device->CreateContext(0);
                break;                
            }
        }
    }
}

Status OpenCLCpuAdapterAcc::Init(Context *context, LayerParam *param, LayerResource *resource, const std::vector<Blob *> &inputs,
                        const std::vector<Blob *> &outputs) {
    if(cpu_adapter_acc_ == NULL) {
       return Status(TNNERR_OPENCL_ACC_INIT_ERROR, "cpu adapter acc is null"); 
    }
    AbstractLayerAcc::Init(context, param, resource, inputs, outputs);

    ocl_context_ = dynamic_cast<OpenCLContext *>(context);
    if (ocl_context_ == nullptr) {
        return Status(TNNERR_NULL_PARAM, "OpenCL Context Convert failed");
    }    

    for(auto input : inputs) {
        auto desc = input->GetBlobDesc();
        desc.device_type = impl_device_type_;
        desc.data_format = DATA_FORMAT_AUTO;
        cpu_blob_in_.push_back(new Blob(desc, true));        
    }

    for(auto output : outputs) {
        auto desc = output->GetBlobDesc();
        desc.device_type = impl_device_type_;
        desc.data_format = DATA_FORMAT_AUTO;
        cpu_blob_out_.push_back(new Blob(desc, true));        
    }

    cpu_adapter_acc_->Init(impl_device_context_, param, resource, cpu_blob_in_, cpu_blob_out_);
    
    return TNN_OK;
}

OpenCLCpuAdapterAcc::~OpenCLCpuAdapterAcc() {
}

Status OpenCLCpuAdapterAcc::Reshape(const std::vector<Blob *> &inputs, const std::vector<Blob *> &outputs) {
    for(int i = 0; i < inputs.size(); ++i) {
        auto device_input = inputs[i];
        auto cpu_input = cpu_blob_in_[i];
        auto dims = device_input->GetBlobDesc().dims;
        cpu_input->GetBlobDesc().dims = dims;
    }   

    for(int i = 0; i < outputs.size(); ++i) {
        auto device_output = outputs[i];
        auto cpu_output = cpu_blob_out_[i];
        auto dims = device_output->GetBlobDesc().dims;
        cpu_output->GetBlobDesc().dims = dims;
    }   
 
    cpu_adapter_acc_->Reshape(cpu_blob_in_, cpu_blob_out_);
    return TNN_OK;
}

Status OpenCLCpuAdapterAcc::Forward(const std::vector<Blob *> &inputs, const std::vector<Blob *> &outputs) {
    void* command_queue;
    ocl_context_->GetCommandQueue(&command_queue);
    //convert data from opencl to cpu
    for(int i = 0; i < inputs.size(); ++i) {
        auto device_input = inputs[i];
        auto cpu_input = cpu_blob_in_[i];
        auto dims = cpu_input->GetBlobDesc().dims;
        BlobConverter blob_converter(device_input);
        MatConvertParam param;
        if(DATA_FORMAT_NCHW == cpu_input->GetBlobDesc().data_format) {
            Mat mat(DEVICE_NAIVE, NCHW_FLOAT, cpu_input->GetBlobDesc().dims, cpu_input->GetHandle().base);
            blob_converter.ConvertToMat(mat, param, command_queue);
        } else {
            Mat mat(DEVICE_NAIVE, NCHW_FLOAT, cpu_input->GetBlobDesc().dims);
            blob_converter.ConvertToMat(mat, param, command_queue);
            float* src_data = reinterpret_cast<float*>(mat.GetData());
            float* dst_data = reinterpret_cast<float*>(cpu_input->GetHandle().base); 
            DataFormatConverter::ConvertFromNCHWToNCHW4Float(src_data, dst_data, dims[0], dims[1], dims[2], dims[3]);
        }
    }
        
    cpu_adapter_acc_->Forward(cpu_blob_in_, cpu_blob_out_);
   
    //convert data from cpu to opencl
    for(int i = 0; i < outputs.size(); ++i) {
        auto device_output = outputs[i];
        auto cpu_output = cpu_blob_out_[i];
        auto dims = cpu_output->GetBlobDesc().dims;
        device_output->GetBlobDesc().dims = dims;

        BlobConverter blob_converter(device_output);
        MatConvertParam param;    
        if(DATA_FORMAT_NCHW == cpu_output->GetBlobDesc().data_format) {
            Mat mat(DEVICE_NAIVE, NCHW_FLOAT, cpu_output->GetBlobDesc().dims, cpu_output->GetHandle().base);
            blob_converter.ConvertFromMat(mat, param, command_queue); 
        } else {
            Mat mat(DEVICE_NAIVE, NCHW_FLOAT, cpu_output->GetBlobDesc().dims);
            float* src_data = reinterpret_cast<float*>(cpu_output->GetHandle().base); 
            float* dst_data = reinterpret_cast<float*>(mat.GetData());
            DataFormatConverter::ConvertFromNCHW4ToNCHWFloat(src_data, dst_data, dims[0], dims[1], dims[2], dims[3]);
            blob_converter.ConvertFromMat(mat, param, command_queue); 
        }
    }
    
    return TNN_OK;
}

std::vector<DataFormat> OpenCLCpuAdapterAcc::SupportDataFormat(DataType data_type, int dims_size) {
    std::vector<DataFormat> support_list;
    if (dims_size == 4) {
        support_list.push_back(DATA_FORMAT_NHC4W4);
    }   
    return support_list;
}

}

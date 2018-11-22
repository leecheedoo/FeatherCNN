//Tencent is pleased to support the open source community by making FeatherCNN available.

//Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.

//Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//in compliance with the License. You may obtain a copy of the License at
//
//https://opensource.org/licenses/BSD-3-Clause
//
//Unless required by applicable law or agreed to in writing, software distributed
//under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//CONDITIONS OF ANY KIND, either express or implied. See the License for the
//specific language governing permissions and limitations under the License.

#include "feather_generated.h"
#include "blob.h"
#include "fp16/fp16.h"

#include "booster/helper.h"

#include "common.h"

namespace feather
{
template<class Dtype>
void Blob<Dtype>::Alloc()
{
    size_t dim_byte = _num * _channels * _height * _width * sizeof(Dtype);
    _data = (Dtype*) _mm_malloc(dim_byte, 32);
}
template<class Dtype>
void Blob<Dtype>::Free()
{
	if (this->_data)
	{
		free(this->_data);
		this->_data = NULL;
	}
}

template<class Dtype>
void Blob<Dtype>::ReshapeWithRealloc(const Blob<Dtype> *p_blob)
{
    int num      = p_blob->num();
    int channels = p_blob->channels();
    int height   = p_blob->height();
    int width    = p_blob->width();

    ReshapeWithRealloc(num, channels, height, width);
}

template<class Dtype>
void Blob<Dtype>::ReshapeWithRealloc(int num, int channels, int height, int width)
{
    // LOGI("Reallc: (%d %d %d %d) to (%d %d %d %d)", _num, _channels, _height, _width, num, channels, height, width);
    int elem_size = num * channels * height * width;
    Realloc(elem_size);
    this->_num      = num;
    this->_channels = channels;
    this->_height   = height;
    this->_width    = width;
}

template<class Dtype>
void Blob<Dtype>::Realloc(size_t elem_size)
{
    if(elem_size > this->data_size())
    {
        Free();
        _data = (Dtype*) _mm_malloc(elem_size * sizeof(Dtype), 32);
    }
}

template<class Dtype>
void Blob<Dtype>::FromProto(const void *proto_in)//proto MUST be of type BlobProto*
{
    bool use_fp16_data = false;
    const BlobProto* proto = (const BlobProto*) proto_in;
    this->_num = proto->num();
    this->_channels = proto->channels();
    this->_height = proto->height();
    this->_width = proto->width();
    size_t data_length;
    data_length = VectorLength(proto->data());
    //printf("data length %d & %d\n", data_length, VectorLength(proto->data_fp16()));
    if(data_length == 0)
    {
	    data_length = VectorLength(proto->data_fp16());
	    //printf("LOADING FROM FP16 DATA LEN %zu\n", data_length);
	    use_fp16_data = true;
    }
    else
    {
	if(VectorLength(proto->data_fp16()) > 0)
	{
    //printf("LOADING FROM FP16 DATA LEN %zu\n", VectorLength(proto->data_fp16()));
		fprintf(stderr, "Fatal error: this model have FP16 and FP32 data in the same blob, aborting...\n");
		exit(-1);
	}
    }


    if (_num * _channels * _height * _width == data_length)
    {
        this->Alloc();
        for (int i = 0; i < data_length; ++i)
        {
            if (use_fp16_data)
            {
                if (std::is_same<Dtype, uint16_t>::value)
                {
                    this->_data[i] = proto->data_fp16()->Get(i);
                }
                else
                {
                    this->_data[i] = fp16_ieee_to_fp32_value(proto->data_fp16()->Get(i));
                }
            }
            else
            {
                if (std::is_same<Dtype, uint16_t>::value)
                {

                    this->_data[i] = hs_floatToHalf(proto->data()->Get(i));
                    // printf("%f, ", hs_halfToFloat(this->_data[i]));
                }
                else
                {
                    this->_data[i] = proto->data()->Get(i);
                }
              //this->_data[i] = proto->data()->Get(i);
            }
        }
    }
    else
    {
        //Error handling
    }
}

#ifdef FEATHER_OPENCL

template<class Dtype>
int Blob<Dtype>::WriteToDevice(cl_command_queue queue, const Dtype* data, size_t data_size)
{
    int error_num;
    /* Image2D in the near future */
    Dtype* data_mapped = (Dtype* )clEnqueueMapBuffer(queue, _data_cl,
                                            CL_TRUE, CL_MAP_WRITE, 0,
                                            data_size * sizeof(Dtype),
                                            0, NULL, NULL, &error_num);

    if (!checkSuccess(error_num))
    {
      LOGE("fatal error: WriteBuffer Mapping memory objects failed. %s: %s", __FILE__, __LINE__);
      return 1;
    }

    memcpy(data_mapped, data, data_size * sizeof(Dtype));

    error_num = clEnqueueUnmapMemObject(queue, _data_cl, data_mapped, 0, NULL, NULL);
    if (!checkSuccess(error_num)){
      LOGE("fatal error: WriteBuffer Unmapping memory objects failed. %s: %s", __FILE__, __LINE__);
      return 1;
    }
    return 0;

}

template<class Dtype>
int Blob<Dtype>::ReadFromDevice(cl_command_queue queue, Dtype* data, size_t data_size) const
{
    int error_num;

    Dtype* data_mapped = (Dtype* )clEnqueueMapBuffer(queue, _data_cl,
                                                  CL_TRUE, CL_MAP_READ,
                                                  0, data_size * sizeof(Dtype), 0, NULL,
                                                  NULL, &error_num);
    if (!checkSuccess(error_num)){
      LOGE("fatal error: ReadBuffer Mapping memory objects failed. %s: %s", __FILE__, __LINE__);
      return 1;
    }

    memcpy(data, data_mapped, data_size * sizeof(Dtype));

    error_num = clEnqueueUnmapMemObject(queue, _data_cl, data_mapped, 0,  NULL, NULL);

    if (!checkSuccess(error_num)){
      LOGE("fatal error: ReadBuffer Unmapping memory objects failed. %s:  %s", __FILE__, __LINE__);
      return 1;
    }
    return 0;
}

template<class Dtype>
int Blob<Dtype>::ReadFromDeviceCHW(cl_command_queue queue, float* data) const
{
    int error_num;
    size_t data_size = this->data_size_padded_c();

    uint16_t* data_half = (uint16_t*)clEnqueueMapBuffer(queue, _data_cl, CL_TRUE, CL_MAP_READ,
                                            0, data_size * sizeof(uint16_t), 0, NULL, NULL, &error_num);
    if (!checkSuccess(error_num)){
      LOGE("fatal error: ReadBuffer Mapping memory objects failed.");
      return -1;
    }
    for (int i = 0; i < _channels; ++i) {
      for (int j = 0; j < _height * _width; ++j) {
        int dst_idx = i * _height * _width + j;
        int src_idx = j * this->get_channels_padding() + i;
        data[dst_idx] = hs_halfToFloat(data_half[src_idx]);
      }
    }
    error_num = clEnqueueUnmapMemObject(queue, _data_cl, data_half, 0, NULL, NULL);
    if (!checkSuccess(error_num)){
      LOGE("fatal error: ReadBuffer Unmapping memory objects failed.");
      return -1;
    }
    return 0;

}

template<class Dtype>
int Blob<Dtype>::AllocDevice(cl_context context, size_t data_size)
{
    if (!this->_data_cl)
    {
        int error_num;
        /* Image2D in the near future */
        _data_cl = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, data_size * sizeof(Dtype), NULL, &error_num);

        if (!checkSuccess(error_num))
        {
            LOGE("Failed to create OpenCL buffers[%d]. %s: %s", error_num, __FILE__, __LINE__);
            return 1;
        }
    }
    return 0;
}

template<class Dtype>
int Blob<Dtype>::FreeDevice()
{
    int error_num;
    if (this->_data_cl){
        error_num = clReleaseMemObject(_data_cl);
        if (!checkSuccess(error_num))
        {
            LOGE("Failed to release mem object. %s: %s", __FILE__, __LINE__);
            return 1;
        }
        _data_cl = NULL;
    }
    return 0;
}
#endif



template class Blob<float>;
template class Blob<uint16_t>;
template class Blob<char>;
};

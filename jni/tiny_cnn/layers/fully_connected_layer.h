/*
    Copyright (c) 2013, Taiga Nomi
    All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY 
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "tiny_cnn/layers/layer.h"
#include "tiny_cnn/util/product.h"

namespace tiny_cnn {

template<typename Activation>

/*全连接层，作为网络末尾的输出层，最终的输出结果就是网络的最终判决结果，即一个标签向量
这里面主要实现了前向传播算法和反向传播算法的全连接版*/
class fully_connected_layer : public layer<Activation> {
public:
    typedef layer<Activation> Base;
    CNN_USE_LAYER_MEMBERS;

    fully_connected_layer(cnn_size_t in_dim, cnn_size_t out_dim, bool has_bias = true)
        : Base(in_dim, out_dim, size_t(in_dim) * out_dim, has_bias ? out_dim : 0), has_bias_(has_bias) {}

    size_t connection_size() const override {
        return size_t(in_size_) * out_size_ + size_t(has_bias_) * out_size_;
    }

    size_t fan_in_size() const override {
        return in_size_;
    }

    size_t fan_out_size() const override {
        return out_size_;
    }

    const vec_t& forward_propagation(const vec_t& in, size_t index) override {
        auto& ws = this->get_worker_storage(index);
        vec_t &a = ws.a_;
        vec_t &out = ws.output_;

        for_i(parallelize_, out_size_, [&](int i) {
            a[i] = float_t(0);
            for (cnn_size_t c = 0; c < in_size_; c++) {
                a[i] += W_[c*out_size_ + i] * in[c];
            }

            if (has_bias_)
                a[i] += b_[i];
        });

        for_i(parallelize_, out_size_, [&](int i) {
            out[i] = h_.f(a, i);
        });
        CNN_LOG_VECTOR(out, "[fc]forward");

        return next_ ? next_->forward_propagation(out, index) : out;
    }

    const vec_t& back_propagation(const vec_t& curr_delta, size_t index) override {
        auto& ws = this->get_worker_storage(index);
        const vec_t& prev_out = prev_->output(static_cast<int>(index));
        const activation::function& prev_h = prev_->activation_function();
        vec_t& prev_delta = ws.prev_delta_;
        vec_t& dW = ws.dW_;
        vec_t& db = ws.db_;

        for (cnn_size_t c = 0; c < this->in_size_; c++) {
            // propagate delta to previous layer
            // prev_delta[c] += current_delta[r] * W_[c * out_size_ + r]
            prev_delta[c] = vectorize::dot(&curr_delta[0], &W_[c*out_size_], out_size_);
            prev_delta[c] *= prev_h.df(prev_out[c]);
        }

        for_(parallelize_, 0, size_t(out_size_), [&](const blocked_range& r) {
            // accumulate weight-step using delta
            // dW[c * out_size + i] += current_delta[i] * prev_out[c]
            for (cnn_size_t c = 0; c < in_size_; c++)
                vectorize::muladd(&curr_delta[r.begin()], prev_out[c], r.end() - r.begin(), &dW[c*out_size_ + r.begin()]);

            if (has_bias_) {
                for (int i = r.begin(); i < r.end(); i++)
                    db[i] += curr_delta[i];
            }
        });

        CNN_LOG_VECTOR(curr_delta, "[fc]curr_delta");
        CNN_LOG_VECTOR(prev_delta, "[fc]prev_delta");
        CNN_LOG_VECTOR(dW, "[fc]dW");
        CNN_LOG_VECTOR(db, "[fc]db");

        return prev_->back_propagation(ws.prev_delta_, index);
    }

    const vec_t& back_propagation_2nd(const vec_t& current_delta2) override {
        const vec_t& prev_out = prev_->output(0);
        const activation::function& prev_h = prev_->activation_function();

        for (cnn_size_t c = 0; c < in_size_; c++) 
            for (cnn_size_t r = 0; r < out_size_; r++)
                Whessian_[c*out_size_ + r] += current_delta2[r] * sqr(prev_out[c]);

        if (has_bias_) {
            for (cnn_size_t r = 0; r < out_size_; r++)
                bhessian_[r] += current_delta2[r];
        }

        for (cnn_size_t c = 0; c < in_size_; c++) { 
            prev_delta2_[c] = float_t(0);

            for (cnn_size_t r = 0; r < out_size_; r++) 
                prev_delta2_[c] += current_delta2[r] * sqr(W_[c*out_size_ + r]);

            prev_delta2_[c] *= sqr(prev_h.df(prev_out[c]));
        }
        CNN_LOG_VECTOR(current_delta2, "[fc]curr-delta2");
        CNN_LOG_VECTOR(prev_delta2_, "[fc]prev-delta2");

        return prev_->back_propagation_2nd(prev_delta2_);
    }

    std::string layer_type() const override { return "fully-connected"; }

protected:
    bool has_bias_;
};

} // namespace tiny_cnn

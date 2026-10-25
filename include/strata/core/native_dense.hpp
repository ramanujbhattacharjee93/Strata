#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace strata::core {
class WeightTable;

/// STRATA_HC_Q8=1 or 2: the hyper-connection projections' Q8_0 bytes for the verify read (1: next to the BF16 rows,
/// 2: instead of them)
bool hc_q8_requested();
/// STRATA_HC_Q8=2: the layers' BF16 hyper-connection down/up rows are not loaded (see hc_q8_replaced_names)
bool hc_q8_replace();

// Experimental GDN/QSA/shared-expert projection overrides. Upload unchanged native GGUF
// blocks once, then attach them to the matching canonical WeightRef. Unsupported
// types retain their canonical paths. Owns one Q8_1 scratch vector shared by all
// these projections, so use one ordered session stream and keep this object
// alive until all graphs that reference it have been destroyed and synchronized.
class NativeDense {
public:
    NativeDense() = default;
    ~NativeDense();
    NativeDense(const NativeDense&) = delete;
    NativeDense& operator=(const NativeDense&) = delete;
    /// With layer_hi >= 0, only the `blk.<l>.` matrices with layer_lo <= l < layer_hi are uploaded (a layer
    /// split's stage holds its own layers' projections, not the whole model's); the others keep data == nullptr.
    bool load(const std::vector<std::string>& shards, WeightTable& table, std::string& err,
              bool include_ple_key = false, int64_t layer_lo = 0, int64_t layer_hi = -1);
    /// Plan v0.3 P1: the canonical tensor names `load` would serve natively from these shards (eligible name,
    /// supported type, 2-D), read from the GGUF headers only - so the canonical arena can skip them.
    static bool served_names(const std::vector<std::string>& shards, bool include_ple_key,
                             std::set<std::string>& out, std::string& err);
    /// Layer split: load only blocks [lb, le) (every other `blk.N.` projection belongs to another GPU's stage; the
    /// PLE tensors are loaded everywhere).  Process-wide, read by the next `load`; (-1, -1) = all layers.
    static void set_layer_range(int lb, int le);
    /// Layer split: the device bytes `load` would allocate for the matrices of layers [lb, le) - the same walk and
    /// filters, each matrix rounded up to the granule cudaMalloc backs it with - reading the GGUF headers and `table`
    /// only, so it allocates nothing and needs no device (#1238).  `table` must hold every tensor the shards carry.
    static bool weight_bytes_for(const std::vector<std::string>& shards, WeightTable& table, bool include_ple_key,
                                 int64_t lb, int64_t le, uint64_t& out, std::string& err);
    /// #326: a native pack whose `blk.1.ple_key.weight` row is unquantized (iq_pack --compat-bf16 of a GGUF key
    /// the native kernel also reads, e.g. OrcaRouter's IQ3_XXS) serves the PLE from that row, so it is taken out
    /// of `skip` and `load` does not upload the GGUF key over it.  A quantized row leaves `skip` unchanged.
    static bool keep_unquantized_ple_key(const std::string& pack_dir, std::set<std::string>& skip, std::string& err);
    /// STRATA_HC_Q8=2: the layers' hyper-connection down/up projections that the GGUF stores as Q8_0 - the canonical
    /// arena skips their BF16 rows and `load` serves them as Q8_0 only (the decode read; the prompt path dequantizes
    /// them to the same BF16 bits per layer, `hc_q8_to_bf16`).
    static bool hc_q8_replaced_names(const std::vector<std::string>& shards, std::set<std::string>& out, std::string& err);
    uint64_t weight_bytes() const { return bytes_; }
    /// What the device lost to `load`: each matrix rounded up to the granule `cudaMalloc` backs it with.
    uint64_t allocated_bytes() const { return allocated_; }
    size_t tensor_count() const { return weights_.size() - packed_keys_.size(); }

private:
    std::vector<void*> weights_;
    std::vector<const void*> packed_keys_;   // GGUF-layout pointers registered with STRATA_Q8_PACKED=1
    void* scratch_ = nullptr;
    uint64_t bytes_ = 0;
    uint64_t allocated_ = 0;
};
} // namespace strata::core

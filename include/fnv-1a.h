/*
 * Copied from https://stackoverflow.com/questions/34597260/stdhash-value-on-char-value-and-not-on-memory-address
 */

#ifndef FNV_1A_INCLUDED
#define FNV_1A_INCLUDED

template <typename ResultT, ResultT OffsetBasis, ResultT Prime>
class basic_fnv1a final
{

  static_assert(std::is_unsigned<ResultT>::value, "need unsigned integer");

public:

  using result_type = ResultT;

private:

  result_type state_ {};

public:

  constexpr
  basic_fnv1a() noexcept : state_ {OffsetBasis}
  {
  }

  void
  update(const void *const data, const std::size_t size) noexcept
  {
    const auto cdata = static_cast<const unsigned char *>(data);
    auto acc = this->state_;
    for (auto i = std::size_t {}; i < size; ++i)
      {
        const auto next = std::size_t {cdata[i]};
        acc = (acc ^ next) * Prime;
      }
    this->state_ = acc;
  }

  constexpr result_type
  digest() const noexcept
  {
    return this->state_;
  }

};

using fnv1a_32 = basic_fnv1a<std::uint32_t,
                             UINT32_C(2166136261),
                             UINT32_C(16777619)>;

using fnv1a_64 = basic_fnv1a<std::uint64_t,
                             UINT64_C(14695981039346656037),
                             UINT64_C(1099511628211)>;

template <std::size_t Bits>
struct fnv1a;

template <>
struct fnv1a<32>
{
  using type = fnv1a_32;
};

template <>
struct fnv1a<64>
{
  using type = fnv1a_64;
};

template <std::size_t Bits>
using fnv1a_t = typename fnv1a<Bits>::type;

#endif

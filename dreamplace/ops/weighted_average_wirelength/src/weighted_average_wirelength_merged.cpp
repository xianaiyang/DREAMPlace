#include "utility/src/torch.h"
#include "utility/src/Msg.h"
#include "weighted_average_wirelength/src/functional.h"

DREAMPLACE_BEGIN_NAMESPACE

/// @brief Compute weighted average wirelength and gradient.
/// WL = \sum_i x_i*exp(x_i/gamma) / \sum_i exp(x_i/gamma) - \sum_i x_i*exp(-x_i/gamma) / \sum_i x_i*exp(-x_i/gamma),
/// where x_i is pin location.
///
/// @param x x location of pins.
/// @param y y location of pins.
/// @param flat_netpin consists pins of each net, pins belonging to the same net are abutting to each other.
/// @param netpin_start bookmark for the starting index of each net in flat_netpin. The length is number of nets. The last entry equals to the number of pins.
/// @param net_mask whether compute the wirelength for a net or not
/// @param net_weights weight of nets
/// @param num_nets number of nets.
/// @param inv_gamma the inverse number of gamma coefficient in weighted average wirelength.
/// @param partial_wl wirelength in x and y directions of each net. The first half is the wirelength in x direction, and the second half is the wirelength in y direction.
/// @param grad_tensor back-propagated gradient from previous stage.
/// @param grad_x_tensor gradient in x direction.
/// @param grad_y_tensor gradient in y direction.
/// @return 0 if successfully done.
template <typename T>
int computeWeightedAverageWirelengthMergedLauncher(
    const T *x, const T *y,
    const int *flat_netpin,
    const int *netpin_start,
    const unsigned char *net_mask,
    int num_nets,
    const T *inv_gamma,
    T *partial_wl,
    T *grad_intermediate_x, T *grad_intermediate_y,
    int num_threads)
{
    int chunk_size = DREAMPLACE_STD_NAMESPACE::max(int(2 * num_nets / num_threads / 16), 1);
#pragma omp parallel for num_threads(num_threads) schedule(dynamic, chunk_size)
    for (int i = 0; i < 2 * num_nets; ++i)
    {
        int ii = i >> 1;
        if (net_mask[ii])
        {
            const T *values;
            T *grads;
            if (i & 1)
            {
                values = y;
                grads = grad_intermediate_y;
            }
            else
            {
                values = x;
                grads = grad_intermediate_x;
            }

            // int degree = netpin_start[ii+1]-netpin_start[ii];
            T x_max = -FLT_MAX;
            T x_min = FLT_MAX;
            for (int j = netpin_start[ii]; j < netpin_start[ii + 1]; ++j)
            {
                T xx = values[flat_netpin[j]];
                x_max = DREAMPLACE_STD_NAMESPACE::max(xx, x_max);
                x_min = DREAMPLACE_STD_NAMESPACE::min(xx, x_min);
            }

            T xexp_x_sum = 0;
            T xexp_nx_sum = 0;
            T exp_x_sum = 0;
            T exp_nx_sum = 0;
            for (int j = netpin_start[ii]; j < netpin_start[ii + 1]; ++j)
            {
                T xx = values[flat_netpin[j]];
                T exp_x = exp((xx - x_max) * (*inv_gamma));
                T exp_nx = exp((x_min - xx) * (*inv_gamma));

                xexp_x_sum += xx * exp_x;
                xexp_nx_sum += xx * exp_nx;
                exp_x_sum += exp_x;
                exp_nx_sum += exp_nx;
            }

            partial_wl[i] = xexp_x_sum / exp_x_sum - xexp_nx_sum / exp_nx_sum;

            T b_x = (*inv_gamma) / (exp_x_sum);
            T a_x = (1.0 - b_x * xexp_x_sum) / exp_x_sum;
            T b_nx = -(*inv_gamma) / (exp_nx_sum);
            T a_nx = (1.0 - b_nx * xexp_nx_sum) / exp_nx_sum;

            for (int j = netpin_start[ii]; j < netpin_start[ii + 1]; ++j)
            {
                T xx = values[flat_netpin[j]];
                T exp_x = exp((xx - x_max) * (*inv_gamma));
                T exp_nx = exp((x_min - xx) * (*inv_gamma));

                grads[flat_netpin[j]] = (a_x + b_x * xx) * exp_x - (a_nx + b_nx * xx) * exp_nx;
            }
        }
    }

#define CHECK_FLAT(x) AT_ASSERTM(!x.is_cuda() && x.ndimension() == 1, #x "must be a flat tensor on GPU")
#define CHECK_EVEN(x) AT_ASSERTM((x.numel() & 1) == 0, #x "must have even number of elements")
#define CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x "must be contiguous")

    /// @brief Compute weighted average wirelength and gradient.
    /// WL = \sum_i x_i*exp(x_i/gamma) / \sum_i exp(x_i/gamma) - \sum_i x_i*exp(-x_i/gamma) / \sum_i x_i*exp(-x_i/gamma),
    /// where x_i is pin location.
    ///
    /// @param pos location of pins, x array followed by y array.
    /// @param flat_netpin consists pins of each net, pins belonging to the same net are abutting to each other.
    /// @param netpin_start bookmark for the starting index of each net in flat_netpin. The length is number of nets. The last entry equals to the number of pins.
    /// @param net_weights weight of nets
    /// @param net_mask whether compute the wirelength for a net or not
    /// @param inv_gamma the inverse number of gamma coefficient in weighted average wirelength.
    /// @return total wirelength cost.
    std::vector<at::Tensor> weighted_average_wirelength_forward(
        at::Tensor pos,
        at::Tensor flat_netpin,
        at::Tensor netpin_start,
        at::Tensor pin2net_map,
        at::Tensor net_weights,
        at::Tensor net_mask,
        at::Tensor inv_gamma,
        int num_threads)
    {
        CHECK_FLAT(pos);
        CHECK_EVEN(pos);
        CHECK_CONTIGUOUS(pos);
        CHECK_FLAT(flat_netpin);
        CHECK_CONTIGUOUS(flat_netpin);
        CHECK_FLAT(netpin_start);
        CHECK_CONTIGUOUS(netpin_start);
        CHECK_FLAT(net_weights);
        CHECK_CONTIGUOUS(net_weights);
        CHECK_FLAT(net_mask);
        CHECK_CONTIGUOUS(net_mask);
        CHECK_FLAT(pin2net_map);
        CHECK_CONTIGUOUS(pin2net_map);

        int num_nets = netpin_start.numel() - 1;
        int num_pins = pos.numel() / 2;

        // x, y interleave
        at::Tensor partial_wl = at::zeros({num_nets, 2}, pos.options());
        // timed with grad_in yet
        at::Tensor grad_intermediate = at::zeros_like(pos);

        DREAMPLACE_DISPATCH_FLOATING_TYPES(pos.type(), "computeWeightedAverageWirelengthMergedLauncher", [&] {
            computeWeightedAverageWirelengthMergedLauncher<scalar_t>(
                pos.data<scalar_t>(), pos.data<scalar_t>() + num_pins,
                flat_netpin.data<int>(),
                netpin_start.data<int>(),
                net_mask.data<unsigned char>(),
                num_nets,
                inv_gamma.data<scalar_t>(),
                partial_wl.data<scalar_t>(),
                grad_intermediate.data<scalar_t>(), grad_intermediate.data<scalar_t>() + num_pins,
                num_threads);
            if (net_weights.numel())
            {
                partial_wl.mul_(net_weights.view({num_nets, 1}));
            }
        });

        auto wl = partial_wl.sum();
        return {wl, grad_intermediate};
    }

    /// @brief Compute gradient
    /// @param grad_pos input gradient from backward propagation
    /// @param pos locations of pins
    /// @param flat_netpin similar to the JA array in CSR format, which is flattened from the net2pin map (array of array)
    /// @param netpin_start similar to the IA array in CSR format, IA[i+1]-IA[i] is the number of pins in each net, the length of IA is number of nets + 1
    /// @param net_weights weight of nets
    /// @param net_mask an array to record whether compute the where for a net or not
    /// @param inv_gamma a scalar tensor for the parameter in the equation
    at::Tensor weighted_average_wirelength_backward(
        at::Tensor grad_pos,
        at::Tensor pos,
        at::Tensor grad_intermediate,
        at::Tensor flat_netpin,
        at::Tensor netpin_start,
        at::Tensor pin2net_map,
        at::Tensor net_weights,
        at::Tensor net_mask,
        at::Tensor inv_gamma,
        int num_threads)
    {
        CHECK_FLAT(pos);
        CHECK_EVEN(pos);
        CHECK_CONTIGUOUS(pos);
        CHECK_FLAT(flat_netpin);
        CHECK_CONTIGUOUS(flat_netpin);
        CHECK_FLAT(netpin_start);
        CHECK_CONTIGUOUS(netpin_start);
        CHECK_FLAT(net_weights);
        CHECK_CONTIGUOUS(net_weights);
        CHECK_FLAT(net_mask);
        CHECK_CONTIGUOUS(net_mask);
        CHECK_FLAT(pin2net_map);
        CHECK_CONTIGUOUS(pin2net_map);
        CHECK_FLAT(grad_intermediate);
        CHECK_EVEN(grad_intermediate);
        CHECK_CONTIGUOUS(grad_intermediate);

        at::Tensor grad_out = grad_intermediate.mul_(grad_pos);
        //int num_nets = netpin_start.numel() - 1;
        int num_pins = pos.numel() / 2;

        DREAMPLACE_DISPATCH_FLOATING_TYPES(pos.type(), "computeWeightedAverageWirelengthMergedLauncher", [&] {
            if (net_weights.numel())
            {
                integrateNetWeightsLauncher<scalar_t>(
                    flat_netpin.data<int>(),
                    netpin_start.data<int>(),
                    net_mask.data<unsigned char>(),
                    net_weights.data<scalar_t>(),
                    grad_out.data<scalar_t>(), grad_out.data<scalar_t>() + pos.numel() / 2,
                    netpin_start.numel() - 1,
                    num_threads);
            }
        });
        return grad_out;
    }

    DREAMPLACE_END_NAMESPACE

    PYBIND11_MODULE(TORCH_EXTENSION_NAME, m)
    {
        m.def("forward", &DREAMPLACE_NAMESPACE::weighted_average_wirelength_forward, "WeightedAverageWirelength forward");
        m.def("backward", &DREAMPLACE_NAMESPACE::weighted_average_wirelength_backward, "WeightedAverageWirelength backward");
    }
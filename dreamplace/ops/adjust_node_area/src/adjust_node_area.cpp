/**
 * @file   adjust_node_area.cpp
 * @author Zixuan Jiang, Jiaqi Gu, Yibo Lin
 * @date   Dec 2019
 * @brief  Adjust cell area according to congestion map. 
 */

#include "utility/src/torch.h"
#include "utility/src/Msg.h"
#include "utility/src/utils.h"

DREAMPLACE_BEGIN_NAMESPACE

#define CHECK_FLAT(x) AT_ASSERTM(!x.is_cuda() && x.ndimension() == 1, #x "must be a flat tensor on CPU")
#define CHECK_EVEN(x) AT_ASSERTM((x.numel() & 1) == 0, #x "must have even number of elements")
#define CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x "must be contiguous")

template <typename T>
int computeInstanceRoutabilityOptimizationMapLauncher(
    const T *pos_x, const T *pos_y,
    const T *node_size_x, const T *node_size_y,
    const T *routing_utilization_map,
    T xl, T yl,
    T bin_size_x, T bin_size_y,
    int num_nodes,
    int num_bins_x, int num_bins_y,
    int num_threads,
    T *instance_route_area
    )
{
    const T inv_bin_size_x = 1.0 / bin_size_x;
    const T inv_bin_size_y = 1.0 / bin_size_y;

    int chunk_size = DREAMPLACE_STD_NAMESPACE::max(int(num_nodes / num_threads / 16), 1);
#pragma omp parallel for num_threads(num_threads) schedule(dynamic, chunk_size)
    for (int i = 0; i < num_nodes; ++i)
    {
        const T x_max = pos_x[i] + node_size_x[i];
        const T x_min = pos_x[i];
        const T y_max = pos_y[i] + node_size_y[i];
        const T y_min = pos_y[i];

        // compute the bin box that this net will affect
        // We do NOT follow Wuxi's implementation. Instead, we clamp the bounding box.
        int bin_index_xl = int((x_min - xl) * inv_bin_size_x);
        int bin_index_xh = int((x_max - xl) * inv_bin_size_x) + 1;
        bin_index_xl = DREAMPLACE_STD_NAMESPACE::max(bin_index_xl, 0);
        bin_index_xh = DREAMPLACE_STD_NAMESPACE::min(bin_index_xh, num_bins_x);

        int bin_index_yl = int((y_min - yl) * inv_bin_size_y);
        int bin_index_yh = int((y_max - yl) * inv_bin_size_y) + 1;
        bin_index_yl = DREAMPLACE_STD_NAMESPACE::max(bin_index_yl, 0);
        bin_index_yh = DREAMPLACE_STD_NAMESPACE::min(bin_index_yh, num_bins_y);

        T& area = instance_route_area[i];
        area = 0; 
        for (int x = bin_index_xl; x < bin_index_xh; ++x)
        {
            for (int y = bin_index_yl; y < bin_index_yh; ++y)
            {
                T overlap = (DREAMPLACE_STD_NAMESPACE::min(x_max, (x + 1) * bin_size_x) - DREAMPLACE_STD_NAMESPACE::max(x_min, x * bin_size_x)) *
                            (DREAMPLACE_STD_NAMESPACE::min(y_max, (y + 1) * bin_size_y) - DREAMPLACE_STD_NAMESPACE::max(y_min, y * bin_size_y));
                area += overlap * routing_utilization_map[x * num_bins_y + y];
            }
        }
    }

    return 0;
}

at::Tensor adjust_node_area_forward(
    at::Tensor pos,
    at::Tensor node_size_x,
    at::Tensor node_size_y,
    at::Tensor routing_utilization_map,
    double bin_size_x,
    double bin_size_y,
    double xl,
    double yl,
    double xh,
    double yh,
    int num_movable_nodes,
    int num_bins_x,
    int num_bins_y,
    int num_threads
    )
{
    CHECK_FLAT(pos);
    CHECK_EVEN(pos);
    CHECK_CONTIGUOUS(pos);

    CHECK_FLAT(node_size_x);
    CHECK_CONTIGUOUS(node_size_x);

    CHECK_FLAT(node_size_y);
    CHECK_CONTIGUOUS(node_size_y);

    int num_nodes = pos.numel() / 2; 
    at::Tensor instance_route_area = at::empty({num_nodes}, pos.options());

    // compute routability and density optimziation instance area
    DREAMPLACE_DISPATCH_FLOATING_TYPES(pos.type(), "computeInstanceRoutabilityOptimizationMapLauncher", [&] {
        computeInstanceRoutabilityOptimizationMapLauncher<scalar_t>(
            pos.data<scalar_t>(), pos.data<scalar_t>() + num_nodes,
            node_size_x.data<scalar_t>(), node_size_y.data<scalar_t>(),
            routing_utilization_map.data<scalar_t>(),
            xl, yl,
            bin_size_x, bin_size_y,
            num_bins_x, num_bins_y,
            num_movable_nodes,
            num_threads,
            instance_route_area.data<scalar_t>());
    });

    return instance_route_area; 
}

DREAMPLACE_END_NAMESPACE

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m)
{
    m.def("forward", &DREAMPLACE_NAMESPACE::adjust_node_area_forward, "Compute adjusted area for routability optimization");
}
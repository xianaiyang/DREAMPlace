/**
 * @file   DetailedPlaceDBUtils.h
 * @author Yibo Lin
 * @date   Jan 2019
 */
#ifndef _DREAMPLACE_UTILITY_DETAILEDPLACEDBUTILS_H
#define _DREAMPLACE_UTILITY_DETAILEDPLACEDBUTILS_H

DREAMPLACE_BEGIN_NAMESPACE

/// @brief make a database for detailed placement. 
/// Only movable nodes will be moved. Fixed nodes and filler nodes are fixed. 
/// 
/// @param init_pos initial x/y location of nodes, including movable nodes, fixed nodes, and filler nodes, [0, num_movable_nodes) are movable nodes, [num_movable_nodes, num_nodes-num_filler_nodes) are fixed nodes, [num_nodes-num_filler_nodes, num_nodes) are filler nodes. x values are at the first half of the array, y values are at the second half of the array
/// @param node_size_x width of nodes, including movable nodes, fixed nodes, and filler nodes, [0, num_movable_nodes) are movable nodes, [num_movable_nodes, num_nodes-num_filler_nodes) are fixed nodes, [num_nodes-num_filler_nodes, num_nodes) are filler nodes
/// @param node_size_y height of nodes, including movable nodes, fixed nodes, and filler nodes, same as node_size_x
/// @param flat_net2pin_map consists pins of each net, pins belonging to the same net are abutting to each other. 
/// @param flat_net2pin_start_map bookmark for the starting index of each net in flat_net2pin_map. The length is number of nets. The last entry equals to the number of pins. 
/// @param pin2net_map maps pin index to net index. 
/// @param flat_node2pin_map consists pins of each node, pins belonging to the same node are abutting to each other. 
/// @param flat_node2pin_start_map bookmark for the starting index of each node in flat_node2pin_map. The length is number of nodes. The last entry equals to the number of pins. 
/// @param pin2node_map maps pin index to node index. 
/// @param pin_offset_x pin offset in x direction 
/// @param pin_offset_y pin offset in y direction 
/// @param net_mask whether a net should be considered for wirelength 
/// @param xl left edge of bounding box of layout area 
/// @param yl bottom edge of bounding box of layout area 
/// @param xh right edge of bounding box of layout area 
/// @param yh top edge of bounding box of layout area 
/// @param site_width width of a placement site 
/// @param row_height height of a placement row 
/// @param num_bins_x number of bins in horizontal direction 
/// @param num_bins_y number of bins in vertical direction 
/// @param num_nodes total number of nodes, including movable nodes, fixed nodes, and filler nodes; fixed nodes are in the range of [num_movable_nodes, num_nodes-num_filler_nodes)
/// @param num_movable_nodes number of movable nodes, movable nodes are in the range of [0, num_movable_nodes)
/// @param num_terminal_NIs number of terminal_NIs, essential fixed IO pins, in the range of [num_movable_nodes+num_terminal, num_nodes-num_filler_nodes)
/// @param num_filler_nodes number of filler nodes, filler nodes are in the range of [num_nodes-num_filler_nodes, num_nodes)
template <typename T>
DetailedPlaceDB<T> make_placedb(
        at::Tensor init_pos,
        at::Tensor pos, 
        at::Tensor node_size_x,
        at::Tensor node_size_y,
        at::Tensor flat_region_boxes, 
        at::Tensor flat_region_boxes_start, 
        at::Tensor node2fence_region_map, 
        at::Tensor flat_net2pin_map, 
        at::Tensor flat_net2pin_start_map, 
        at::Tensor pin2net_map, 
        at::Tensor flat_node2pin_map, 
        at::Tensor flat_node2pin_start_map, 
        at::Tensor pin2node_map, 
        at::Tensor pin_offset_x, 
        at::Tensor pin_offset_y, 
        at::Tensor net_mask, 
        double xl, 
        double yl, 
        double xh, 
        double yh, 
        double site_width, double row_height, 
        int num_bins_x, 
        int num_bins_y,
        int num_movable_nodes, 
        int num_terminal_NIs, 
        int num_filler_nodes
        )
{
    DetailedPlaceDB<T> db; 
    int num_nodes = init_pos.numel()/2;

    db.init_x = init_pos.data<T>(); 
    db.init_y = init_pos.data<T>()+num_nodes; 
    db.node_size_x = node_size_x.data<T>(); 
    db.node_size_y = node_size_y.data<T>(); 
    db.flat_region_boxes = flat_region_boxes.data<T>();
    db.flat_region_boxes_start = flat_region_boxes_start.data<int>();
    db.node2fence_region_map = node2fence_region_map.data<int>();
    db.x = pos.data<T>(); 
    db.y = pos.data<T>()+num_nodes; 
    db.flat_net2pin_map = flat_net2pin_map.data<int>(); 
    db.flat_net2pin_start_map = flat_net2pin_start_map.data<int>(); 
    db.pin2net_map = pin2net_map.data<int>(); 
    db.flat_node2pin_map = flat_node2pin_map.data<int>(); 
    db.flat_node2pin_start_map = flat_node2pin_start_map.data<int>(); 
    db.pin2node_map = pin2node_map.data<int>(); 
    db.pin_offset_x = pin_offset_x.data<T>(); 
    db.pin_offset_y = pin_offset_y.data<T>(); 
    db.net_mask = net_mask.data<unsigned char>();
    db.xl = xl; 
    db.yl = yl; 
    db.xh = xh; 
    db.yh = yh; 
    db.site_width = site_width; 
    db.row_height = row_height; 
    db.bin_size_x = (xh-xl)/num_bins_x; 
    db.bin_size_y = (yh-yl)/num_bins_y; 
    db.num_bins_x = num_bins_x; 
    db.num_bins_y = num_bins_y; 
    db.num_sites_x = (xh-xl)/site_width; 
    db.num_sites_y = (yh-yl)/row_height; 
    db.num_nodes = num_nodes - num_filler_nodes - num_terminal_NIs; 
    db.num_movable_nodes = num_movable_nodes; 
    db.num_nets = flat_net2pin_start_map.numel()-1;
    db.num_pins = pin2net_map.numel();
    db.num_regions = flat_region_boxes_start.numel()-1;

    return db; 
}

DREAMPLACE_END_NAMESPACE

#endif

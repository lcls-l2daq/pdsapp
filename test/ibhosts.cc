/**************************************
 *  Simple program to dump HCA attrs  *
 **************************************/

#include <infiniband/verbs.h>
#include <stdio.h>

int main()
{
  //
  //  Fetch and dump the local devices
  //
  int num_devices;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);

  printf("Found %d devices\n", num_devices);
  if (num_devices==0) return 1;

  printf("%10.10s %10.10s %40.40s %40.40s\n",
         "name","dev_name","dev_path","ibdev_path");

  for(int i=0; i<num_devices; i++)
    printf("%10.10s %10.10s %40.40s %40.40s\n",
           device_list[i]->name,
           device_list[i]->dev_name,
           device_list[i]->dev_path,
           device_list[i]->ibdev_path);
  
  struct ibv_context* context = ibv_open_device(device_list[0]);
  if (context==0) {
    perror("Failed to create context");
    return -1;
  }

  //
  //  Fetch and dump the device attributes
  //
  struct ibv_device_attr attr;
  if (ibv_query_device(context, &attr) < 0) {
    perror("Failed to query device");
    return -1;
  }

  // Fun with macros
#define printq(f,v) printf("%20.20s: %"#f"\n",#v,attr.v)
  printq(s,fw_ver);
  printq(lx,node_guid);
  printq(lx,sys_image_guid);
  printq(lx,max_mr_size);
  printq(lx,page_size_cap);
  printq(i,max_qp);
  printq(i,max_qp_wr);
  printq(x,device_cap_flags);
  printq(i,max_sge);
  printq(i,max_sge_rd);
  printq(i,max_cq);
  printq(i,max_cqe);
  printq(i,max_mr);
  printq(i,max_pd);
  printq(i,max_qp_rd_atom);
  printq(i,max_ee_rd_atom);
  printq(i,max_res_rd_atom);
  printq(i,max_qp_init_rd_atom);
  printq(i,max_ee_init_rd_atom);
  printq(i,max_ee);
  printq(i,max_rdd);
  printq(i,max_mw);
  printq(i,max_raw_ipv6_qp);
  printq(i,max_raw_ethy_qp);
  printq(i,max_mcast_grp);
  printq(i,max_mcast_qp_attach);
  printq(i,max_total_mcast_qp_attach);
  printq(i,max_ah);
  printq(i,max_fmr);
  printq(i,max_map_per_fmr);
  printq(i,max_srq);
  printq(i,max_srq_wr);
  printq(i,max_srq_sge);
  printq(i,max_pkeys);
  printq(i,local_ca_ack_delay);
  printq(i,phys_port_cnt);
#undef printq

  return 1;
}

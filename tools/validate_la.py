import numpy as np
import os
import sys
import re

##########
# config #
##########

d = 20
output_path = "subs_milestone_3/sub_007_result/logs/run"
out_files = os.listdir(output_path)
out_files_filtered = []
config_files_filtered = []
for of in out_files:
    if "output" in of:
        out_files_filtered.append(os.path.join(output_path, of))
    if "config" in of:
        config_files_filtered.append(os.path.join(output_path, of))
        
#########################
# read all output files #
#########################

decisions_dict = {}
for of_path in out_files_filtered:
    of = open(of_path, "r")
    pid = int(re.findall("proc[0-9]+\.", of_path)[0][4:-1])
    decisions_list = of.read().split("\n")[:-1]
    decisions_dict[pid] = decisions_list
    #print(len(decisions_list))
    
#########################
# read all config files #
#########################

config_proposals_dict = {}
for cf_path in config_files_filtered:
    cf = open(cf_path, "r")
    pid = int(re.findall("proc[0-9]+\.", cf_path)[0][4:-1])
    config_proposals_list = cf.read().split("\n")[1:-1]
    config_proposals_dict[pid] = config_proposals_list
    #print(len(config_proposals_list))
    
#########
# tests #
#########

# loop over decidsions
for i in range(d):
    
    # select d-th proposal from config of each process
    dth_prop_dict = {}
    
    # select d-th decision of each process
    for pid_i in config_proposals_dict.keys():
        prop_d_pid_i = config_proposals_dict[pid_i][i].split(" ")
        dth_prop_dict[pid_i] = prop_d_pid_i
  
    # select d-th decision of each process    
    dth_dec_dict = {}
    longest_dec_d = []
    longest_pid = 0
    
    for pid_i in decisions_dict.keys():
        dec_d_pid_i = decisions_dict[pid_i][i].split(" ")
        dth_dec_dict[pid_i] = dec_d_pid_i    
    
    # get all proposet values in this round
    dth_dec_union_of_proposals = set(sum(dth_prop_dict.values(), []))
    #print(dth_dec_union_of_proposals)
    
    ###################
    # validity test 1 #
    ###################
    
    # check if the decided set contains the proposed set
    for pid_i in dth_dec_dict.keys(): # only for those who have an output
        dth_dec_pid_i  = dth_dec_dict [pid_i] # the decided set
        dth_prop_pid_i = dth_prop_dict[pid_i] # the proposed set
        #print(f"[VALIDITY1] Process pid {pid_i} proposed: {dth_prop_pid_i}, decided: {dth_dec_pid_i} in round {i}")

        assert set(dth_prop_pid_i).issubset(dth_dec_pid_i), f"Proposed set {dth_prop_pid_i} is not a subset of decided set {dth_dec_pid_i} for process pid {pid_i} in round {i}"

    ###################
    # validity test 2 #
    ###################
    
    # no creation, test if decisions were actually proposed by someone
    for pid_i in dth_dec_dict.keys(): # only for those who have an output
        dth_dec_pid_i  = dth_dec_dict [pid_i] # the decided set
        
        assert set(dth_dec_pid_i).issubset(dth_dec_union_of_proposals), f"Proposed set {dth_prop_pid_i} is not a subset of all proposed values {dth_dec_union_of_proposals} for process pid {pid_i} in round {i}"
    
    
    ######################
    # consistency test 1 #
    ######################
    
    # select the longest decision i.e. a superset of all other processes
    if len(dec_d_pid_i) > len(longest_dec_d):
        longest_dec_d = dec_d_pid_i
        longest_pid = pid_i  
            
    #compare all decision sets against the longest one in the current round
    for pid_i in dth_dec_dict.keys():
        #print(dth_dec_dict[pid_i])

        dth_dec_pid_i= dth_dec_dict[pid_i]
        
        #print(f"[CONSISTENCY1] Comparing decision {i} between process {pid_i} and {longest_pid} (with longest decided set in round {i})")

        # loop over int values of decision
        for dec_i in dth_dec_pid_i:
            
            # check if all ints are contained in the longest set i.e. a susbset
            assert dec_i in longest_dec_d, f"[ERROR] {dec_i} of pid {pid_i} is not contained in {longest_dec_d} of pid {longest_pid}"
            
    ######################
    # consistency test 2 #
    ######################
    
    # test all pairwise decisions if they compare
    for pid_i in dth_dec_dict.keys():
        dth_dec_pid_i = dth_dec_dict[pid_i]
        for pid_j in dth_dec_dict.keys():
            dth_dec_pid_j = dth_dec_dict[pid_j]
            
            #print(f"[CONSISTENCY2] Comparing decision {i} between process {pid_i} and {pid_j}")
            assert set(dth_dec_pid_i).issubset(dth_dec_pid_j) or set(dth_dec_pid_j).issubset(dth_dec_pid_i), f"[ERROR]"
            
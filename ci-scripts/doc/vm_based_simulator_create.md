<table style="border-collapse: collapse; border: none;">
  <tr style="border-collapse: collapse; border: none;">
    <td style="border-collapse: collapse; border: none;">
      <a href="http://www.openairinterface.org/">
         <img src="../../doc/images/oai_final_logo.png" alt="" border=3 height=50 width=150>
         </img>
      </a>
    </td>
    <td style="border-collapse: collapse; border: none; vertical-align: center;">
      <b><font size = "5">OAI CI Virtual-Machine-based Test Environment: create a VM instance</font></b>
    </td>
  </tr>
</table>

**Table of Contents**

[[_TOC_]]

# 1. Introduction #

```bash
$ cd /tmp/CI-raphael
$ ./ci-scripts/oai-ci-vm-tool create --help
OAI CI VM script
   Original Author: Raphael Defosseux
   Requirements:
     -- uvtool uvtool-libvirt apt-cacher
     -- xenial image already synced
   Default:
     -- eNB with USRP

Usage:
------
    oai-ci-vm-tool create [OPTIONS]

Mandatory Options:
--------
    --job-name #### OR -jn ####
    Specify the name of the Jenkins job.

    --build-id #### OR -id ####
    Specify the build ID of the Jenkins job.

 # OpenAirInterface Build Variants
    --variant enb-usrp        OR -v1     ( build and test  )
    --variant phy-sim         OR -v3     ( build and test  )
    --variant cppcheck        OR -v4     ( build and test  )
    --variant gnb-usrp        OR -v5     ( build and test  )
    --variant nr-ue-usrp      OR -v6     ( build and test  )
    --variant enb-ethernet    OR -v7     ( build and test  )
    --variant ue-ethernet     OR -v8     ( build and test  )
 # non-OSA Build Variants
    --variant flexran-rtc     OR -v10    ( build and test non-OSA )
 # OpenAirInterface Test Variants
    --variant l1-sim          OR -v20    ( test  )
    --variant rf-sim          OR -v21    ( test  )
    --variant l2-sim          OR -v22    ( test  )
    Specify the variant to build.

    --help OR -h
    Print this help message.
```

# 2. Detailed Description #

Source file concerned: `ci-scripts/createVM.sh`

## 2.1. create_vm function ##

This is the function that is being called from the main oai-vm-tool script.

The main purpose is to start a VM instance:

```bash
uvt-kvm create $VM_NAME release=xenial --memory $VM_MEMORY --cpu $VM_CPU --disk $VM_DISK --unsafe-caching --template ci-scripts/template-host.xml
```

Variables are set in the main script based on the options.

`--unsafe-caching` option is used because our VM instances are throw-away's. 

`--template ci-scripts/template-host.xml` is used to duplicate the CPU properties to the VM instance. **VERY IMPORTANT to build OAI**

## 2.2. Lock / Unlock functions ##

There are `acquire_vm_create_lock` and `release_vm_create_lock` functions.

Creating Virtual Machines instances in parallel **creates a lot of stress** on the host server HW. If you launch creations in parallel (Jenkins pipeline could do it) or you are several people working on the same host server, this mechanism atomizes the creation process and wait until the previous VM creation is finished.

# 3. Typical Usage #

```bash
$ cd /tmp/CI-raphael
$ ./ci-scripts/oai-ci-vm-tool create --job-name raphael --build-id 1 --variant phy-sim
# or a more **unique approach**
$ ./ci-scripts/oai-ci-vm-tool create -jn toto -id 1 -v2
```

The Jenkins pipeline uses the master job name as `job-name` option and the job-build ID.

Try to be unique if you are several developers working on the same host server.

Finally, typically I never use the `create` command. I use directly the build command that checks if VM is created and if not, will create it. See next step.

---

Next step: [how to build an OAI variant](./vm_based_simulator_build.md)

You can also go back to the [CI dev main page](./ci_dev_home.md)


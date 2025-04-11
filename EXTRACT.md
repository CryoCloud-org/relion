
#### Development


Start an interactive session with the development container:
```
docker run -it --rm -v $(pwd):/data relion-dev:tag
```

#### Extracting Particles from Micrographs



```
relion_preprocess --i jobs/CtfFind/1/micrographs_ctf.star --coord_dir jobs/Topaz/2/ --coord_suffix _autopick.star --part_star jobs/Extract/3/particles.star --part_dir jobs/Extract/3/ --extract --extract_size 128 --scale 128 --norm --bg_radius 48 --invert_contrast --float16
```


Env = staging
Project = 66
Job = 5

```
relion_preprocess --i jobs/Select/3/micrographs.star --coord_dir jobs/CryoCloudPicker/4/ --coord_suffix _autopick.star --part_star jobs/Extract/5/particles.star --part_dir jobs/Extract/5/ --extract --extract_size 260 --scale 84 --norm --bg_radius 31 --invert_contrast --float16
```

Goal to develop a flag for Extract to filter a all_particles.star file to a subset of particles, based on input 
image name:

Files:
- jobs/Select/3/micrographs.star
- jobs/CryoCloudPicker/4/particles.star



MotionCorr/1/datasets/85/10346/data/20181213_PS2_3_3/


```
relion_preprocess_mpi --i jobs/Select/21/micrographs.star --coord_dir jobs/CryoCloudPicker/22/ --coord_suffix _autopick.star --part_star jobs/Extract/25/particles.star --part_dir jobs/Extract/25/ --extract --extract_size 312 --scale 104 --norm --bg_radius 39 --invert_contrast --float16 
```

Select/21/


Extract/25/jobs/MotionCorr/19/datasets/7/10932/data/Dosefracs/20210221_opanova_mjr_SSG_A3_TEM4/

WORKS!

```
rm -rf /data/jobs/Extract/26 && cd /data && /app/relion/build/bin/relion_preprocess_mpi --i jobs/Select/21/micrographs.star --coord_dir jobs/CryoCloudPicker/22/ --coord_suffix _autopick.star --part_star jobs/Extract/26/particles.star --part_dir jobs/Extract/26/ --extract --extract_size 312 --scale 104 --norm --bg_radius 39 --invert_contrast --float16 
```
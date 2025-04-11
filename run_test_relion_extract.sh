#!/bin/bash

# test_relion.sh - Script to test Relion preprocessing functionality

# Clear previous output directory if it exists
rm -rf /data/jobs/Extract/26

# Change to data directory

cd /app/relion/data

# Run Relion preprocessing with MPI
relion_preprocess \
  --i jobs/Select/21/micrographs.star \
  --all_agg_coords jobs/CryoCloudPicker/22/cryocloudpicker_particles.star \
  --part_star /app/relion/data/jobs/Extract/26/particles.star \
  --part_dir /app/relion/data/jobs/Extract/26/ \
  --extract \
  --extract_size 312 \
  --scale 104 \
  --norm \
  --bg_radius 39 \
  --invert_contrast

# Report exit status
EXIT_STATUS=$?
if [ $EXIT_STATUS -eq 0 ]; then
  echo "✅ Relion preprocessing completed successfully"
else
  echo "❌ Relion preprocessing failed with exit code $EXIT_STATUS"
fi
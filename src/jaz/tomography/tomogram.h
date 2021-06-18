#ifndef DYN_TOMOGRAM_H
#define DYN_TOMOGRAM_H

#include <src/jaz/image/buffered_image.h>
#include <vector>
#include <src/jaz/gravis/t4Matrix.h>
#include <src/jaz/optics/optics_data.h>
#include <src/ctf.h>
#include "motion/spline_2D_deformation.h"

class ParticleIndex;
class ParticleSet;

class Tomogram
{
	public:
		
		Tomogram();
			
			
			bool hasOptics, hasImage, hasDeformations;
			OpticsData optics;
			int frameCount;
			double handedness, fractionalDose;
			
			BufferedImage<float> stack;
			std::vector<gravis::d4Matrix> projectionMatrices;
			std::vector<Spline2DDeformation> imageDeformations;
			
			std::vector<CTF> centralCTFs;
			std::vector<double> cumulativeDose;
			gravis::d3Vector centre;
			int w0, h0, d0;
			std::vector<int> frameSequence;
			std::string name, tiltSeriesFilename, opticsGroupName, fiducialsFilename;
			double defocusSlope;
		
			
		gravis::d2Vector projectPoint(
				const gravis::d3Vector& p, int frame) const;
		
		bool isVisible(
				const gravis::d3Vector& p, int frame, double radius) const;
		
		std::vector<bool> determineVisiblity(
				const std::vector<gravis::d3Vector>& trajectory, double radius) const;
		
		
		double getFrameDose() const;

		
		BufferedImage<float> computeDoseWeight(int boxSize, double binning) const;
		BufferedImage<float> computeNoiseWeight(int boxSize, double binning, double overlap = 2.0) const;

		double getDepthOffset(int frame, gravis::d3Vector position) const;
		CTF getCtf(int frame, gravis::d3Vector position) const;
		int getLeastDoseFrame() const;

		gravis::d3Vector computeCentreOfMass(
				const ParticleSet& particleSet,
				const std::vector<ParticleIndex>& particle_indices) const;

		Tomogram extractSubstack(gravis::d3Vector position, int width, int height) const;
		Tomogram FourierCrop(double factor, int num_threads, bool downsampleData = true) const;

		bool hasFiducials();
};


#endif
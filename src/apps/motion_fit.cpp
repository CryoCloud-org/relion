
#include <unistd.h>
#include <string.h>
#include <fstream>

#include <src/args.h>
#include <src/image.h>
#include <src/fftw.h>
#include <src/complex.h>
#include <src/metadata_table.h>
#include <src/backprojector.h>
#include <src/euler.h>
#include <src/jaz/image_log.h>
#include <src/jaz/slice_helper.h>
#include <src/jaz/spectral_helper.h>
#include <src/jaz/filter_helper.h>
#include <src/jaz/backprojection_helper.h>
#include <src/jaz/volume_converter.h>
#include <src/jaz/complex_io.h>
#include <src/jaz/fftw_helper.h>
#include <src/jaz/resampling_helper.h>
#include <src/jaz/ctf_helper.h>
#include <src/jaz/defocus_refinement.h>
#include <src/jaz/magnification_refinement.h>
#include <src/jaz/refinement_helper.h>
#include <src/jaz/stack_helper.h>
#include <src/jaz/tilt_refinement.h>
#include <src/jaz/motion_refinement.h>
#include <src/jaz/image_op.h>
#include <src/jaz/refinement_program.h>
#include <src/jaz/damage_helper.h>
#include <src/jaz/fsc_helper.h>
#include <src/jaz/gp_motion_fit.h>
#include <src/jaz/gradient_descent.h>
#include <src/jaz/distribution_helper.h>
#include <src/jaz/parallel_ft.h>
#include <src/jaz/d3x3/dsyev2.h>

#include <src/jaz/motion_em.h>

#include <omp.h>

using namespace gravis;

class MotionFitProg : public RefinementProgram
{
    public:

        MotionFitProg();

            bool unregGlob, noGlobOff, paramEstim,
                debugOpt, diag, expKer, global_init;
            int maxIters;
            double dmga, dmgb, dmgc, dosePerFrame,
                sig_vel, sig_div, sig_acc,
                k_cutoff, maxStep, maxDistDiv,
                param_rV, param_rD, param_thresh;

        int readMoreOptions(IOParser& parser, int argc, char *argv[]);
        int _init();
        int _run();

        void prepMicrograph(
                int g,
                std::vector<ParFourierTransformer>& fts,
                const std::vector<Image<RFLOAT>>& dmgWeight,
                std::vector<std::vector<Image<Complex>>>& movie,
                std::vector<std::vector<Image<RFLOAT>>>& movieCC,
                std::vector<gravis::d2Vector>& positions,
                std::vector<std::vector<gravis::d2Vector>>& initialTracks,
                std::vector<d2Vector>& globComp);

        void estimateMotion(
                std::vector<ParFourierTransformer>& fts,
                const std::vector<Image<RFLOAT>>& dmgWeight);

        d2Vector estimateParams(
                std::vector<ParFourierTransformer>& fts,
                const std::vector<Image<double>>& dmgWeight,
                int k_out, double sig_v_0, double sig_d_0,
                double sig_v_step, double sig_d_step,
                int maxIters);

        void evaluateParams(
                std::vector<ParFourierTransformer>& fts,
                const std::vector<Image<double>>& dmgWeight,
                int k_out,
                const std::vector<d2Vector>& sig_vals,
                std::vector<double>& TSCs);

        void computeWeights(
                double sig_vel_nrm, double sig_acc_nrm, double sig_div_nrm,
                const std::vector<d2Vector>& positions, int fc,
                std::vector<double>& velWgh,
                std::vector<double>& accWgh,
                std::vector<std::vector<std::vector<double>>>& divWgh);

        std::vector<std::vector<d2Vector>> optimize(const std::vector<std::vector<Image<RFLOAT>>>& movieCC,
                const std::vector<std::vector<d2Vector>>& inTracks,
                double sig_vel_px,
                double sig_acc_px,
                double sig_div_px,
                const std::vector<d2Vector>& positions,
                const std::vector<d2Vector>& globComp,
                double step, double minStep, double minDiff,
                long maxIters, double inertia);

        void updateFCC(const std::vector<std::vector<Image<Complex>>>& movie,
                const std::vector<std::vector<d2Vector>>& tracks,
                const MetaDataTable& mdt,
                std::vector<Image<RFLOAT>>& tables,
                std::vector<Image<RFLOAT>>& weights0,
                std::vector<Image<RFLOAT>>& weights1);

        void writeOutput(
                const std::vector<std::vector<d2Vector>>& tracks,
                const std::vector<Image<RFLOAT>>& fccData,
                const std::vector<Image<RFLOAT>>& fccWeight0,
                const std::vector<Image<RFLOAT>>& fccWeight1,
                const std::vector<d2Vector>& positions,
                std::string outPath, int mg,
                double visScale);

        d2Vector interpolateMax(
                const std::vector<d2Vector>& all_sig_vals,
                const std::vector<double>& all_TSCs);
};

int main(int argc, char *argv[])
{
    MotionFitProg mt;

    int rc0 = mt.init(argc, argv);
    if (rc0 != 0) return rc0;

    int rc1 = mt.run();
    if (rc1 != 0) return rc1;
}

MotionFitProg::MotionFitProg()
:   RefinementProgram(false, true)
{
}

int MotionFitProg::readMoreOptions(IOParser& parser, int argc, char *argv[])
{
    dmga = textToFloat(parser.getOption("--dmg_a", "Damage model, parameter a", " 3.40"));
    dmgb = textToFloat(parser.getOption("--dmg_b", "                        b", "-1.06"));
    dmgc = textToFloat(parser.getOption("--dmg_c", "                        c", "-0.54"));

    dosePerFrame = textToFloat(parser.getOption("--fdose", "Electron dose per frame (in e^-/A^2)", "1"));

    sig_vel = textToFloat(parser.getOption("--s_vel", "Velocity sigma [Angst/dose]", "1.6"));
    sig_div = textToFloat(parser.getOption("--s_div", "Divergence sigma [Angst]", "500.0"));
    sig_acc = textToFloat(parser.getOption("--s_acc", "Acceleration sigma [Angst/dose]", "-1.0"));

    global_init = parser.checkOption("--gi", "Initialize with global trajectories instead of loading them from metadata file");

    expKer = parser.checkOption("--exp_k", "Use exponential kernel instead of sq. exponential");

    k_cutoff = textToFloat(parser.getOption("--k_cut", "Freq. cutoff (in pixels)", "-1.0"));
    maxIters = textToInteger(parser.getOption("--max_iters", "Maximum number of iterations", "10000"));
    maxStep = textToFloat(parser.getOption("--max_step", "Maximum step size", "0.05"));

    unregGlob = parser.checkOption("--unreg_glob", "Do not regularize global component of motion");
    noGlobOff = parser.checkOption("--no_glob_off", "Do not compute initial per-particle offsets");

    debugOpt = parser.checkOption("--debug_opt", "Write optimization debugging info");
    diag = parser.checkOption("--diag", "Write out diagnostic data");

    parser.addSection("Parameter estimation");

    paramEstim = parser.checkOption("--params", "Estimate parameters instead of motion");
    param_rV = textToFloat(parser.getOption("--r_vel", "Test s_vel +/- r_vel * s_vel", "0.5"));
    param_rD = textToFloat(parser.getOption("--r_div", "Test s_div +/- r_div * s_div", "0.2"));
    param_thresh = textToFloat(parser.getOption("--pthresh", "Abort when relative TSC change is smaller than this", "0.1"));

    if (paramEstim && k_cutoff < 0)
    {
        std::cerr << "\nParameter estimation requires a freq. cutoff (--k_cut).\n";
        return 1138;
    }

    if (!global_init && corrMicFn == "")
    {
        std::cerr << "\nWarning: in the absence of a corrected_micrographs.star file (--corr_mic), global paths are used for initialization.\n";
        global_init = true;
    }

    return 0;
}

int MotionFitProg::_init()
{
    return 0;
}

int MotionFitProg::_run()
{
    std::vector<ParFourierTransformer> fts(nr_omp_threads);

    loadInitialMovieValues();

    std::vector<Image<RFLOAT>> dmgWeight = DamageHelper::damageWeights(
        s, angpix, firstFrame, fc, dosePerFrame, dmga, dmgb, dmgc);

    int k_out = sh;

    for (int i = 1; i < sh; i++)
    {
        if (freqWeight1D[i] <= 0.0)
        {
            k_out = i;
            break;
        }
    }

    std::cout << "max freq. = " << k_out << " px\n";

    for (int f = 0; f < fc; f++)
    {
        dmgWeight[f].data.xinit = 0;
        dmgWeight[f].data.yinit = 0;

        if (k_cutoff > 0.0)
        {
            std::stringstream stsf;
            stsf << f;
            dmgWeight[f] = FilterHelper::ButterworthEnvFreq2D(dmgWeight[f], k_cutoff-1, k_cutoff+1);

            ImageOp::multiplyBy(dmgWeight[f], freqWeight);
        }
    }

    double t0 = omp_get_wtime();

    if (paramEstim)
    {
        d2Vector par = estimateParams(
            fts, dmgWeight, k_out, sig_vel, sig_div,
            sig_vel*param_rV, sig_div*param_rD, 10);

        std::cout << "opt = " << par << "\n";
    }
    else
    {
        estimateMotion(fts, dmgWeight);
    }


    double t1 = omp_get_wtime();
    double diff = t1 - t0;
    std::cout << "elapsed (total): " << diff << " sec\n";

    return 0;
}

void MotionFitProg::prepMicrograph(
        int g, std::vector<ParFourierTransformer>& fts,
        const std::vector<Image<double>>& dmgWeight,
        std::vector<std::vector<Image<Complex>>>& movie,
        std::vector<std::vector<Image<double>>>& movieCC,
        std::vector<d2Vector>& positions,
        std::vector<std::vector<d2Vector>>& initialTracks,
        std::vector<d2Vector>& globComp)
{
    const int pc = mdts[g].numberOfObjects();

    movie = loadMovie(g, pc, fts); // throws exceptions

    std::vector<double> sigma2 = StackHelper::powerSpectrum(movie);

    #pragma omp parallel for num_threads(nr_omp_threads)
    for (int p = 0; p < pc; p++)
    for (int f = 0; f < fc; f++)
    {
        MotionRefinement::noiseNormalize(movie[p][f], sigma2, movie[p][f]);
    }

    positions = std::vector<gravis::d2Vector>(pc);

    for (int p = 0; p < pc; p++)
    {
        mdts[g].getValue(EMDL_IMAGE_COORD_X, positions[p].x, p);
        mdts[g].getValue(EMDL_IMAGE_COORD_Y, positions[p].y, p);
    }

    if (!paramEstim)
    {
        std::cout << "    computing initial correlations...\n";
    }

    movieCC = MotionRefinement::movieCC(
            projectors[0], projectors[1], obsModel, mdts[g], movie,
            sigma2, dmgWeight, fts, nr_omp_threads);

    initialTracks = std::vector<std::vector<d2Vector>>(pc);

    if (global_init)
    {
        std::vector<Image<RFLOAT>> ccSum = MotionRefinement::addCCs(movieCC);
        std::vector<gravis::d2Vector> globTrack = MotionRefinement::getGlobalTrack(ccSum);
        std::vector<gravis::d2Vector> globOffsets;

        if (noGlobOff)
        {
            globOffsets = std::vector<d2Vector>(pc, d2Vector(0,0));
        }
        else
        {
            globOffsets = MotionRefinement::getGlobalOffsets(
                    movieCC, globTrack, 0.25*s, nr_omp_threads);
        }

        if (diag)
        {
            std::string tag = outPath + "/" + getMicrographTag(g);
            std::string path = tag.substr(0, tag.find_last_of('/'));
            mktree(path);

            ImageLog::write(ccSum, tag + "_CCsum", CenterXY);
        }

        for (int p = 0; p < pc; p++)
        {
            initialTracks[p] = std::vector<d2Vector>(fc);

            for (int f = 0; f < fc; f++)
            {
                if (unregGlob)
                {
                    initialTracks[p][f] = globOffsets[p];
                }
                else
                {
                    initialTracks[p][f] = globTrack[f] + globOffsets[p];
                }
            }
        }

        globComp = unregGlob? globTrack : std::vector<d2Vector>(fc, d2Vector(0,0));
    }
    else
    {
        const double outputScale = movie_angpix / angpix;
        const double inputScale = coords_angpix / movie_angpix;

        globComp = std::vector<d2Vector>(fc, d2Vector(0,0));

        if (unregGlob)
        {
            for (int f = 0; f < fc; f++)
            {
                double sx, sy;
                micrograph.getShiftAt(f+1, 0, 0, sx, sy, false);

                globComp[f] = outputScale * d2Vector(sx, sy);
            }
        }

        for (int p = 0; p < pc; p++)
        {
            initialTracks[p] = std::vector<d2Vector>(fc);

            for (int f = 0; f < fc; f++)
            {
                double sx, sy;
                micrograph.getShiftAt(
                    f+1, inputScale * positions[p].x, inputScale * positions[p].y, sx, sy, true);

                initialTracks[p][f] = outputScale * d2Vector(sx,sy) - globComp[f];
            }
        }
    }
}

void MotionFitProg::estimateMotion(
        std::vector<ParFourierTransformer>& fts,
        const std::vector<Image<double>>& dmgWeight)
{
    std::vector<Image<RFLOAT>>
            tables(nr_omp_threads),
            weights0(nr_omp_threads),
            weights1(nr_omp_threads);

    for (int i = 0; i < nr_omp_threads; i++)
    {
        FscHelper::initFscTable(sh, fc, tables[i], weights0[i], weights1[i]);
    }

    const double sig_vel_nrm = dosePerFrame * sig_vel / angpix;
    const double sig_acc_nrm = dosePerFrame * sig_acc / angpix;
    const double sig_div_nrm = dosePerFrame * sig_div / angpix;

    int pctot = 0;

    // initialize parameter-estimation:

    for (long g = g0; g <= gc; g++)
    {
        std::cout << "micrograph " << (g+1) << " / " << mdts.size() <<"\n";

        const int pc = mdts[g].numberOfObjects();
        if (pc < 2) continue;

        std::stringstream stsg;
        stsg << g;

        std::vector<std::vector<Image<Complex>>> movie;
        std::vector<std::vector<Image<RFLOAT>>> movieCC;
        std::vector<d2Vector> positions;
        std::vector<std::vector<d2Vector>> initialTracks;
        std::vector<d2Vector> globComp;

        try
        {
            prepMicrograph(
                g, fts, dmgWeight,
                movie, movieCC, positions, initialTracks, globComp);
        }
        catch (RelionError XE)
        {
            std::cerr << "warning: unable to load micrograph #" << (g+1) << "\n";
            continue;
        }

        pctot += pc;

        std::cout << "    optimizing...\n";

        std::vector<std::vector<gravis::d2Vector>> tracks = optimize(
                movieCC, initialTracks,
                sig_vel_nrm, sig_acc_nrm, sig_div_nrm,
                positions, globComp,
                maxStep, 1e-9, 1e-9, maxIters, 0.0);

        updateFCC(movie, tracks, mdts[g], tables, weights0, weights1);

        writeOutput(tracks, tables, weights0, weights1, positions, outPath, g, 30.0);

        for (int i = 0; i < nr_omp_threads; i++)
        {
            tables[i].data.initZeros();
            weights0[i].data.initZeros();
            weights1[i].data.initZeros();
        }

    } // micrographs

    /*Image<RFLOAT> table, weight;

    FscHelper::mergeFscTables(tables, weights0, weights1, table, weight);
    ImageLog::write(table, outPath+"_FCC_data");


    int f_max = fc;
    double total = 0.0;

    std::ofstream fccOut(outPath + "_FCC_perFrame.dat");

    for (int y = 0; y < f_max; y++)
    {
        double avg = 0.0;

        for (int k = k_cutoff+2; k < k_out; k++)
        {
            avg += table(y,k);
        }

        avg /= k_out - k_cutoff - 1;

        fccOut << y << " " << avg << "\n";

        total += avg;
    }

    total /= f_max;

    std::cout << "total: " << total << "\n";*/

}

d2Vector MotionFitProg::estimateParams(
        std::vector<ParFourierTransformer>& fts,
        const std::vector<Image<double>>& dmgWeight,
        int k_out, double sig_v_0, double sig_d_0,
        double sig_v_step, double sig_d_step,
        int maxIters)
{
    std::vector<d2Vector> all_sig_vals(9);

    for (int p = 0; p < 9; p++)
    {
        int vi = (p%3) - 1;
        int di = (p/3) - 1;

        all_sig_vals[p][0] = sig_v_0 + vi * sig_v_step;
        all_sig_vals[p][1] = sig_d_0 + di * sig_d_step;
    }

    std::vector<double> all_TSCs(9);

    std::vector<d2Vector> unknown_sig_vals = all_sig_vals;
    std::vector<double> unknown_TSCs(9);

    std::vector<int> unknown_ind(9);

    for (int p = 0; p < 9; p++)
    {
        unknown_ind[p] = p;
    }

    bool centerBest = false;
    int iters = 0;

    int tot_vi = 0, tot_di = 0;

    while (!centerBest && iters < maxIters)
    {

            std::cout << "evaluating:\n";

            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    std::cout << all_sig_vals[3*i + j] << " \t ";
                }

                std::cout << "\n";
            }

            std::cout << "\n";

        evaluateParams(fts, dmgWeight, k_out, unknown_sig_vals, unknown_TSCs);

        for (int p = 0; p < unknown_ind.size(); p++)
        {
            all_TSCs[unknown_ind[p]] = unknown_TSCs[p];
        }


            std::cout << "result:\n";

            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    std::cout << all_TSCs[3*i + j] << " \t ";
                }

                std::cout << "\n";
            }

            std::cout << "\n";

        int bestIndex = 0;
        double bestTSC = all_TSCs[0];

        for (int p = 0; p < 9; p++)
        {
            if (all_TSCs[p] > bestTSC)
            {
                bestTSC = all_TSCs[p];
                bestIndex = p;
            }
        }

        if (bestIndex == 4)
        {
            return interpolateMax(all_sig_vals, all_TSCs);
        }

        int shift_v = bestIndex % 3 - 1;
        int shift_d = bestIndex / 3 - 1;

        tot_vi += shift_v;
        tot_di += shift_d;

        std::vector<d2Vector> next_sig_vals(9, d2Vector(0,0));
        std::vector<double> next_TSCs(9);
        std::vector<bool> known(9, false);

        for (int p = 0; p < 9; p++)
        {
            int vi = (p%3) - 1;
            int di = (p/3) - 1;

            int vi_next = vi - shift_v;
            int di_next = di - shift_d;

            if (vi_next >= -1 && vi_next <= 1
                && di_next >= -1 && di_next <= 1)
            {
                int p_next = (vi_next + 1) + 3*(di_next + 1);

                next_sig_vals[p_next] = all_sig_vals[p];
                next_TSCs[p_next] = all_TSCs[p];
                known[p_next] = true;
            }
        }

        all_sig_vals = next_sig_vals;
        all_TSCs = next_TSCs;

        unknown_sig_vals.clear();
        unknown_ind.clear();

        for (int p = 0; p < 9; p++)
        {
            if (!known[p])
            {
                int vi = (p%3) - 1 + tot_vi;
                int di = (p/3) - 1 + tot_di;

                all_sig_vals[p][0] = sig_v_0 + vi * sig_v_step;
                all_sig_vals[p][1] = sig_d_0 + di * sig_d_step;

                unknown_sig_vals.push_back(all_sig_vals[p]);
                unknown_ind.push_back(p);
            }
        }

        unknown_TSCs.resize(unknown_ind.size());

        iters++;
    }
}

void MotionFitProg::evaluateParams(
        std::vector<ParFourierTransformer>& fts,
        const std::vector<Image<double>>& dmgWeight,
        int k_out,
        const std::vector<d2Vector>& sig_vals,
        std::vector<double>& TSCs)
{
    const int paramCount = sig_vals.size();
    TSCs.resize(paramCount);

    std::vector<double> sig_v_vals_nrm(paramCount);
    std::vector<double> sig_d_vals_nrm(paramCount);

    for (int i = 0; i < paramCount; i++)
    {
        sig_v_vals_nrm[i] = dosePerFrame * sig_vals[i][0] / angpix;
        sig_d_vals_nrm[i] = dosePerFrame * sig_vals[i][1] / angpix;
    }

    double sig_acc_nrm = dosePerFrame * sig_acc / angpix;

    std::vector<std::vector<Image<RFLOAT>>>
        paramTables(paramCount), paramWeights0(paramCount), paramWeights1(paramCount);

    int pctot = 0;

    for (int i = 0; i < paramCount; i++)
    {
        paramTables[i] = std::vector<Image<RFLOAT>>(nr_omp_threads);
        paramWeights0[i] = std::vector<Image<RFLOAT>>(nr_omp_threads);
        paramWeights1[i] = std::vector<Image<RFLOAT>>(nr_omp_threads);

        for (int j = 0; j < nr_omp_threads; j++)
        {
            FscHelper::initFscTable(sh, fc, paramTables[i][j],
                paramWeights0[i][j], paramWeights1[i][j]);
        }
    }

    for (long g = g0; g <= gc; g++)
    {
        const int pc = mdts[g].numberOfObjects();
        pctot += pc;

        std::cout << "    micrograph " << (g+1) << " / " << mdts.size() << ": "
            << pc << " particles [" << pctot << " total]\n";

        std::cout.flush();

        std::stringstream stsg;
        stsg << g;

        std::vector<std::vector<Image<Complex>>> movie;
        std::vector<std::vector<Image<RFLOAT>>> movieCC;
        std::vector<d2Vector> positions;
        std::vector<std::vector<d2Vector>> initialTracks;
        std::vector<d2Vector> globComp;

        try
        {
            prepMicrograph(
                g, fts, dmgWeight,
                movie, movieCC, positions, initialTracks, globComp);
        }
        catch (RelionError XE)
        {
            std::cerr << "warning: unable to load micrograph #" << (g+1) << "\n";
            continue;
        }

        for (int i = 0; i < paramCount; i++)
        {
            if (debug)
            {
                std::cout << "        evaluating: " << sig_vals[i] << "\n";
            }

            std::vector<std::vector<gravis::d2Vector>> tracks = optimize(
                    movieCC, initialTracks,
                    sig_v_vals_nrm[i], sig_acc_nrm, sig_d_vals_nrm[i],
                    positions, globComp, maxStep, 1e-9, 1e-9, maxIters, 0.0);

            updateFCC(movie, tracks, mdts[g], paramTables[i], paramWeights0[i], paramWeights1[i]);
        }

    } // micrographs

    for (int i = 0; i < paramCount; i++)
    {
        TSCs[i] = FscHelper::computeTsc(
            paramTables[i], paramWeights0[i], paramWeights1[i], k_cutoff+2, k_out);
    }
}

std::vector<std::vector<d2Vector>> MotionFitProg::optimize(
        const std::vector<std::vector<Image<RFLOAT>>>& movieCC,
        const std::vector<std::vector<d2Vector>>& inTracks,
        double sig_vel_px, double sig_acc_px, double sig_div_px,
        const std::vector<d2Vector>& positions,
        const std::vector<d2Vector>& globComp,
        double step, double minStep, double minDiff,
        long maxIters, double inertia)
{
    const double eps = 1e-20;

    if (sig_vel_px < eps)
    {
        std::cerr << "Warning: sig_vel < " << eps << " px. Setting to " << eps << ".\n";
        sig_vel_px = eps;
    }

    if (sig_div_px < eps)
    {
        std::cerr << "Warning: sig_div < " << eps << " px. Setting to " << eps << ".\n";
        sig_div_px = eps;
    }

    const int pc = inTracks.size();

    if (pc == 0) return std::vector<std::vector<d2Vector>>(0);

    const int fc = inTracks[0].size();

    GpMotionFit gpmf(movieCC, sig_vel_px, sig_div_px, sig_acc_px,
                     pc, positions,
                     globComp, nr_omp_threads, expKer);


    std::vector<double> initialCoeffs(2*(pc + pc*(fc-1)));

    gpmf.posToParams(inTracks, initialCoeffs);

    std::vector<double> optPos = GradientDescent::optimize(
            initialCoeffs, gpmf, step, minStep, minDiff, maxIters, inertia, debugOpt);

    std::vector<std::vector<d2Vector>> out(pc, std::vector<d2Vector>(fc));
    gpmf.paramsToPos(optPos, out);

    return out;
}

void MotionFitProg::updateFCC(
        const std::vector<std::vector<Image<Complex>>>& movie,
        const std::vector<std::vector<d2Vector>>& tracks,
        const MetaDataTable& mdt,
        std::vector<Image<RFLOAT>>& tables,
        std::vector<Image<RFLOAT>>& weights0,
        std::vector<Image<RFLOAT>>& weights1)
{
    const int pc = mdt.numberOfObjects();

    #pragma omp parallel for num_threads(nr_omp_threads)
    for (int p = 0; p < pc; p++)
    {
        int threadnum = omp_get_thread_num();

        Image<Complex> pred;
        std::vector<Image<Complex>> obs = movie[p];

        for (int f = 0; f < fc; f++)
        {
            shiftImageInFourierTransform(obs[f](), obs[f](), s, -tracks[p][f].x, -tracks[p][f].y);
        }

        int randSubset;
        mdt.getValue(EMDL_PARTICLE_RANDOM_SUBSET, randSubset, p);
        randSubset -= 1;

        if (randSubset == 0)
        {
            pred = obsModel.predictObservation(projectors[1], mdt, p, true, true);
        }
        else
        {
            pred = obsModel.predictObservation(projectors[0], mdt, p, true, true);
        }

        FscHelper::updateFscTable(obs, pred, tables[threadnum],
                                  weights0[threadnum], weights1[threadnum]);
    }
}

void MotionFitProg::writeOutput(
        const std::vector<std::vector<d2Vector>>& tracks,
        const std::vector<Image<RFLOAT>>& fccData,
        const std::vector<Image<RFLOAT>>& fccWeight0,
        const std::vector<Image<RFLOAT>>& fccWeight1,
        const std::vector<d2Vector>& positions,
        std::string outPath, int mg,
        double visScale)
{
    const int pc = tracks.size();

    if (pc == 0) return;

    const int fc = tracks[0].size();

    std::string tag = getMicrographTag(mg);
    MotionRefinement::writeTracks(tracks, outPath + "/" + tag + "_tracks.star");

    Image<RFLOAT> fccDataSum(sh,fc), fccWeight0Sum(sh,fc), fccWeight1Sum(sh,fc);
    fccDataSum.data.initZeros();
    fccWeight0Sum.data.initZeros();
    fccWeight1Sum.data.initZeros();

    for (int i = 0; i < fccData.size(); i++)
    {
        for (int y = 0; y < fc; y++)
        for (int x = 0; x < sh; x++)
        {
            fccDataSum(y,x) += fccData[i](y,x);
            fccWeight0Sum(y,x) += fccWeight0[i](y,x);
            fccWeight1Sum(y,x) += fccWeight1[i](y,x);
        }
    }

    fccDataSum.write(outPath + "/" + tag + "_FCC_cc.mrc");
    fccWeight0Sum.write(outPath + "/" + tag + "_FCC_w0.mrc");
    fccWeight1Sum.write(outPath + "/" + tag + "_FCC_w1.mrc");

    if (!diag) return;

    // plot graphs here:

    std::vector<std::vector<gravis::d2Vector>>
            centTracks(pc), visTracks(pc), centVisTracks(pc);

    for (int p = 0; p < pc; p++)
    {
        centTracks[p] = std::vector<gravis::d2Vector>(fc);
        visTracks[p] = std::vector<gravis::d2Vector>(fc);
        centVisTracks[p] = std::vector<gravis::d2Vector>(fc);
    }

    std::vector<gravis::d2Vector> globalTrack(fc);

    for (int f = 0; f < fc; f++)
    {
        globalTrack[f] = d2Vector(0,0);

        for (int p = 0; p < pc; p++)
        {
            globalTrack[f] += tracks[p][f];
        }

        globalTrack[f] /= pc;
        for (int p = 0; p < pc; p++)
        {
            centTracks[p][f] = tracks[p][f] - globalTrack[f];
            visTracks[p][f] = positions[p] + visScale * tracks[p][f];
            centVisTracks[p][f] = positions[p] + visScale * centTracks[p][f];
        }
    }

    std::ofstream rawOut(outPath + "/" + tag + "_tracks.dat");
    std::ofstream visOut(outPath + "/" + tag + "_visTracks.dat");
    std::ofstream visOut15(outPath + "/" + tag + "_visTracks_first15.dat");

    for (int p = 0; p < pc; p++)
    {
        rawOut << "#particle " << p << "\n";
        visOut << "#particle " << p << "\n";
        visOut15 << "#particle " << p << "\n";

        for (int f = 0; f < fc; f++)
        {
            rawOut << tracks[p][f].x << " " << tracks[p][f].y << "\n";
            visOut << visTracks[p][f].x << " " << visTracks[p][f].y << "\n";

            if (f < 15) visOut15 << visTracks[p][f].x << " " << visTracks[p][f].y << "\n";
        }

        rawOut << "\n";
        visOut << "\n";
        visOut15 << "\n";
    }

    std::ofstream glbOut(outPath + "/" + tag + "_globTrack.dat");

    for (int f = 0; f < fc; f++)
    {
        glbOut << globalTrack[f].x << " " << globalTrack[f].y << "\n";
    }
}

d2Vector MotionFitProg::interpolateMax(
    const std::vector<d2Vector> &all_sig_vals,
    const std::vector<double> &all_TSCs)
{
    const int parCt = all_sig_vals.size();

    Matrix2D<RFLOAT> A(parCt,6);
    Matrix1D<RFLOAT> b(parCt);

    int bestP = 0;
    double bestTsc = all_TSCs[0];

    for (int p = 0; p < parCt; p++)
    {
        if (all_TSCs[p] > bestTsc)
        {
            bestTsc = all_TSCs[p];
            bestP = p;
        }
    }

    if (bestP != 4)
    {
        std::cerr << "Waring: value not maximal at the center.\n";
        return all_sig_vals[bestP];
    }

    for (int p = 0; p < parCt; p++)
    {
        const double v = all_sig_vals[p][0];
        const double d = all_sig_vals[p][1];

        A(p,0) = v*v;
        A(p,1) = 2.0*v*d;
        A(p,2) = 2.0*v;
        A(p,3) = d*d;
        A(p,4) = 2.0*d;
        A(p,5) = 1.0;

        b(p) = all_TSCs[p];
    }

    const double tol = 1e-20;
    Matrix1D<RFLOAT> x(6);
    solve(A, b, x, tol);

    d2Matrix C2(x(0), x(1),
                x(1), x(3));

    d2Vector l(x(2), x(4));

    d2Matrix C2i = C2;
    C2i.invert();

    d2Vector min = -(C2i * l);

    return min;
}

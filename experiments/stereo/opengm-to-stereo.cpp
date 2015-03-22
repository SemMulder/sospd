/*
 * stereo.cpp
 *
 * Copyright 2014 Alexander Fix
 * See LICENSE.txt for license information
 *
 * Stereo inference using fusion proposals
 * Unary potentials and proposals are loaded from file
 */

#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <boost/program_options.hpp>
#include "multilabel-energy.hpp"
#include "fusion-move.hpp"
#include "sospd.hpp"

typedef MultilabelEnergy::Label Label;
typedef MultilabelEnergy::VarId VarId;

class StereoClique : public Clique {
    public:
        StereoClique(const int* nodes, 
                const std::vector<cv::Mat>& proposals) 
        : m_proposals(proposals) {
            for (int i = 0; i < 3; ++i)
                m_nodes[i] = nodes[i];
        }

        virtual REAL energy(const Label buf[]) const override;
        virtual const VarId* nodes() const override { return m_nodes; }
        virtual size_t size() const override { return 3; }

        static float kappa;
        static float alpha;
        static float scale;
    protected:
        VarId m_nodes[3];
        const std::vector<cv::Mat>& m_proposals;
};

float StereoClique::kappa = 0.001;
float StereoClique::alpha = 10.0;
float StereoClique::scale = 20000;


REAL StereoClique::energy(const Label buf[]) const {
    float disparity[3];
    double energy;
    for (int i = 0; i < 3; ++i)
        disparity[i] = m_proposals[buf[i]].at<float>(m_nodes[i]);
    if (std::abs(disparity[1] - disparity[0]) > alpha
            || std::abs(disparity[2] - disparity[1]) > alpha) {
        energy = kappa;
    } else {
        float curvature = disparity[0] - 2*disparity[1] + disparity[2];
        energy = std::min(curvature*curvature, kappa);
    }
    return energy*scale/kappa;
}


MultilabelEnergy SetupEnergy(const std::vector<cv::Mat>& proposals,
        const std::vector<cv::Mat>& unaries);
std::vector<cv::Mat> ReadUnaries(const std::string& unaryFilename);
std::vector<cv::Mat> ReadProposals(const std::string& proposalFilename);
void ShowImage(const cv::Mat& im);

int width = 0;
int height = 0;
int nproposals = 0;

int main(int argc, char **argv) {
    namespace po = boost::program_options;
    // Variables set by program options
    std::string basename;
    std::string opengmFileName;
    std::string unaryFilename;
    std::string proposalFilename;
    std::string outfilename;

    // Setup and parse options
    po::options_description options("Stereo arguments");
    options.add_options()
        ("help", "Display this help message")
        ("image",
         po::value<std::string>(&basename)->required(),
         "Name of image (without extension)")
        ("kappa", 
         po::value<float>(&StereoClique::kappa)->default_value(0.001),
         "Truncation for stereo prior")
        ("alpha",
         po::value<float>(&StereoClique::alpha)->default_value(10),
         "Max gradient for stereo prior")
        ("lambda",
         po::value<float>(&StereoClique::scale)->default_value(20000),
         "Scale for stereo prior")
        ("output", po::value<std::string>(&outfilename)->required(),
         "Output file name")
        ("ogmFile", po::value<std::string>(&opengmFileName)->required(),
         "Opengm file name")
    ;

    po::positional_options_description positionalOpts;
    positionalOpts.add("image", 1);
    positionalOpts.add("ogmFile", 2);
    positionalOpts.add("output", 3);

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).
                options(options).positional(positionalOpts).run(), vm);
        if (vm.count("help")) {
            std::cout << options;
            exit(0);
        }
        po::notify(vm);
    } catch (std::exception& e) {
        std::cout << "Parsing error: " << e.what() << "\n";
        std::cout << "Usage: denoise [options] basename\n";
        std::cout << options;
        exit(-1);
    }

    unaryFilename = basename + ".unary";
    proposalFilename = basename + ".proposals";

    // Read stored unaries and proposed moves
    std::cout << "Reading proposals...\n";
    std::vector<cv::Mat> proposals = ReadProposals(proposalFilename);
    std::cout << "Reading unaries...\n";
    std::vector<cv::Mat> unaries = ReadUnaries(unaryFilename);
    cv::Mat image(height, width, CV_32FC1);

    std::vector<Label> current(width*height, 0);
    std::cout << "Setting up energy...\n";
    MultilabelEnergy energyFunction = SetupEnergy(proposals, unaries);

    {
        // Read image from opengm output file
        std::ifstream ogmFile(opengmFileName);
        std::string line = "foo";
        while (line != "%states" && line != "")
            std::getline(ogmFile, line);
        if (line == "") {
            std::cout << "Didn't find states\n";
            exit(-1);
        }
        std::getline(ogmFile, line);
        std::stringstream sstream(line);
        for (int i = 0; i < width*height; ++i) {
            int label;
            sstream >> label;
            sstream.ignore(2);
            current[i] = label;
        }

    }

    for (int i = 0; i < height; ++i)
        for (int j = 0; j < width; ++j)
            image.at<float>(i, j) = 
                proposals[current[i*width+j]].at<float>(i, j);

    // Write out results
    image.convertTo(image, CV_8U, 1.0, 0);
    cv::imwrite(outfilename.c_str(), image); 

    REAL energy  = energyFunction.computeEnergy(current);
    std::cout << "Final Energy: " << energy << std::endl;

    return 0;
}

MultilabelEnergy SetupEnergy(const std::vector<cv::Mat>& proposals, 
        const std::vector<cv::Mat>& unary) {
    MultilabelEnergy energy(nproposals);
    energy.addVar(width*height);
    
    // For each 1x3 patch, add in a StereoClique
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width - 2; ++j) {
            int nodes[3] = { i*width+j, i*width+j+1, i*width+j+2 };
            energy.addClique(MultilabelEnergy::CliquePtr(
                        new StereoClique(nodes, proposals)));
        }
    }
    // For each 3x1 patch, add in a StereoClique
    for (int i = 0; i < height-2; ++i) {
        for (int j = 0; j < width; ++j) {
            int nodes[3] = { i*width+j, (i+1)*width+j, (i+2)*width+j };
            energy.addClique(MultilabelEnergy::CliquePtr(
                        new StereoClique(nodes, proposals)));
        }
    }
    // Add the unary terms
    std::vector<REAL> unaryBuf(nproposals);
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            VarId n = i*width + j;
            for (int l = 0; l < nproposals; ++l)
                unaryBuf[l] = REAL(std::round(unary[l].at<float>(i, j)));
            energy.addUnaryTerm(n, unaryBuf);
        }
    }
    return energy;
}

std::vector<cv::Mat> ReadProposals(const std::string& proposalFilename) {
    std::vector<cv::Mat> proposals;

    std::ifstream f(proposalFilename);
    std::string nproposals_s;
    std::getline(f, nproposals_s);
    nproposals = stoi(nproposals_s);
    for (int i = 0; i < nproposals; ++i) {
        std::string size_line;
        std::getline(f, size_line);
        sscanf(size_line.c_str(), "%d %d", &height, &width);
        cv::Mat m(width, height, CV_32FC1);
        for (int j = 0; j < width*height; ++j) {
            std::string line;
            std::getline(f, line);
            m.at<float>(j) = stod(line);
        }
        m = m.t();
        proposals.push_back(m);
    }
    return proposals;
}

std::vector<cv::Mat> ReadUnaries(const std::string& unaryFilename) {
    std::vector<cv::Mat> unaries;

    std::ifstream f(unaryFilename);
    std::string nproposals_s;
    std::getline(f, nproposals_s);
    if (stoi(nproposals_s) != nproposals) {
        std::cout << "Number of proposals in label file " \
            "does not match proposal file\n";
        exit(-1);
    }
    for (int i = 0; i < nproposals; ++i) {
        std::string size_line;
        std::getline(f, size_line);
        int size = std::stoi(size_line);
        if (size != width*height) {
            std::cout << "Size and width*height don't match in Unary file!\n";
            std::cout << "Size: " << size << "\tWidth: " 
                << width << "\tHeight: " << height << "\n";
            exit(-1);
        }
        cv::Mat m(width, height, CV_32FC1);
        for (int j = 0; j < width*height; ++j) {
            std::string line;
            std::getline(f, line);
            m.at<float>(j) = stod(line);
        }
        m = m.t();
        unaries.push_back(m);
    }
    return unaries;
}

void ShowImage(const cv::Mat& im) {
    cv::namedWindow( "Display window", CV_WINDOW_AUTOSIZE );
    cv::imshow( "Display window", im);                   

    cv::waitKey(0);
}

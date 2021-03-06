#include "opencv2/core/core_c.h"
#include "opencv2/core/utility.hpp"
#include "opencv2/xfeatures2d.hpp"
#include "opencv2/opencv.hpp"



//
// use dlib's implementation for facial landmarks,
// if not present, fall back to a precalculated
// 'one-size-fits-all' set of points(based on the mean lfw image)
//
//#define HAVE_DLIB

#ifdef HAVE_DLIB
 #include <dlib/image_processing.h>
 #include <dlib/opencv/cv_image.h>
#endif


#include <vector>
using std::vector;
#include <iostream>
using std::cerr;
using std::endl;

#include "texturefeature.h"
#include "elastic/elasticparts.h"

//#include "profile.h"

using namespace cv;
using namespace TextureFeature;

namespace TextureFeatureImpl
{


//
// this is the most simple one.
//
struct ExtractorPixels : public TextureFeature::Extractor
{
    int resw,resh;

    ExtractorPixels(int resw=0,int resh=0): resw(resw), resh(resh) {}

    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        if (resw>0 && resh>0)
            resize(img, features, Size(resw,resh));
        else
            features = img;
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};


//
// later use gridded histograms the same way as with lbp(h)
//
struct FeatureGrad
{
    int nsec,nrad;
    FeatureGrad(int nsec=45, int nrad=8) : nsec(nsec), nrad(nrad) {}

    int operator() (const Mat &I, Mat &fI) const
    {
        Mat s1, s2, s3(I.size(), CV_32F), s4, s5;
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);
        fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        fI = s3 / (360/nsec);
        fI.convertTo(fI,CV_8U);

        //magnitude(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total());
        //normalize(s3,s4,nrad);
        //s4.convertTo(s5,CV_8U);
        //s5 *= nsec;
        //fI += s5;
        //return nrad*nsec;
        return nsec+1;
    }
};


struct FeatureLbp
{
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                uchar cen = img(r,c);
                v |= (img(r-1,c  ) > cen) << 0;
                v |= (img(r-1,c+1) > cen) << 1;
                v |= (img(r  ,c+1) > cen) << 2;
                v |= (img(r+1,c+1) > cen) << 3;
                v |= (img(r+1,c  ) > cen) << 4;
                v |= (img(r+1,c-1) > cen) << 5;
                v |= (img(r  ,c-1) > cen) << 6;
                v |= (img(r-1,c-1) > cen) << 7;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 256;
    }
};

//
// "Description of Interest Regions with Center-Symmetric Local Binary Patterns"
// (http://www.ee.oulu.fi/mvg/files/pdf/pdf_750.pdf).
//    (w/o threshold)
//
struct FeatureCsLbp
{
    int radius;
    FeatureCsLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c  ) > img(r+R,c  )) << 0;
                v |= (img(r-R,c+R) > img(r+R,c-R)) << 1;
                v |= (img(r  ,c+R) > img(r  ,c-R)) << 2;
                v |= (img(r+R,c+R) > img(r-R,c-R)) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};


//
// / \
// \ /
//
struct FeatureDiamondLbp
{
    int radius;
    FeatureDiamondLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c  ) > img(r  ,c+R)) << 0;
                v |= (img(r  ,c+R) > img(r+R,c  )) << 1;
                v |= (img(r+R,c  ) > img(r  ,c-R)) << 2;
                v |= (img(r  ,c-R) > img(r-R,c  )) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};


//  _ _
// |   |
// |_ _|
//
struct FeatureSquareLbp
{
    int radius;
    FeatureSquareLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c-R) > img(r-R,c+R)) << 0;
                v |= (img(r-R,c+R) > img(r+R,c+R)) << 1;
                v |= (img(r+R,c+R) > img(r+R,c-R)) << 2;
                v |= (img(r+R,c-R) > img(r-R,c-R)) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};

//
// Antonio Fernandez, Marcos X. Alvarez, Francesco Bianconi:
// "Texture description through histograms of equivalent patterns"
//
//    basically, this is just 1/2 of the full lbp-circle (4bits / 16 bins only!)
//
struct FeatureMTS
{
    int operator () (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> img(I);
        Mat_<uchar> fea(I.size(), 0);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                uchar cen = img(r,c);
                v |= (img(r-1,c  ) > cen) << 0;
                v |= (img(r-1,c+1) > cen) << 1;
                v |= (img(r  ,c+1) > cen) << 2;
                v |= (img(r+1,c+1) > cen) << 3;
                fea(r,c) = v;
            }
        }
        fI = fea;
        return 16;
    }
};


//
// just run around in a circle (instead of comparing to the center) ..
//
struct FeatureBGC1
{
    int operator () (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                v |= (img(r-1,c  ) > img(r-1,c-1)) << 0;
                v |= (img(r-1,c+1) > img(r-1,c  )) << 1;
                v |= (img(r  ,c+1) > img(r-1,c+1)) << 2;
                v |= (img(r+1,c+1) > img(r  ,c+1)) << 3;
                v |= (img(r+1,c  ) > img(r+1,c+1)) << 4;
                v |= (img(r+1,c-1) > img(r+1,c  )) << 5;
                v |= (img(r  ,c-1) > img(r+1,c-1)) << 6;
                v |= (img(r-1,c-1) > img(r  ,c-1)) << 7;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 256;
    }
};



//
// Wolf, Hassner, Taigman : "Descriptor Based Methods in the Wild"
// 3.1 Three-Patch LBP Codes
//
struct FeatureTPLbp
{
    int operator () (const Mat &img, Mat &features) const
    {
        Mat_<uchar> I(img);
        Mat_<uchar> fI(I.size(), 0);
        const int R=2;
        for (int r=R; r<I.rows-R; r++)
        {
            for (int c=R; c<I.cols-R; c++)
            {
                uchar v = 0;
                v |= ((I(r,c) - I(r  ,c-2)) > (I(r,c) - I(r-2,c  ))) * 1;
                v |= ((I(r,c) - I(r-1,c-1)) > (I(r,c) - I(r-1,c+1))) * 2;
                v |= ((I(r,c) - I(r-2,c  )) > (I(r,c) - I(r  ,c+2))) * 4;
                v |= ((I(r,c) - I(r-1,c+1)) > (I(r,c) - I(r+1,c+1))) * 8;
                v |= ((I(r,c) - I(r  ,c+2)) > (I(r,c) - I(r+1,c  ))) * 16;
                v |= ((I(r,c) - I(r+1,c+1)) > (I(r,c) - I(r+1,c-1))) * 32;
                v |= ((I(r,c) - I(r+1,c  )) > (I(r,c) - I(r  ,c-2))) * 64;
                v |= ((I(r,c) - I(r+1,c-1)) > (I(r,c) - I(r-1,c-1))) * 128;
                fI(r,c) = v;
            }
        }
        features = fI;
        return 256;
    }
};



//
// Wolf, Hassner, Taigman : "Descriptor Based Methods in the Wild"
// 3.2 Four-Patch LBP Codes (4bits / 16bins only !)
//
struct FeatureFPLbp
{
    int radius;
    FeatureFPLbp(int r=2) : radius(r) {}

    int operator () (const Mat &img, Mat &features) const
    {
        Mat_<uchar> I(img);
        Mat_<uchar> fI(I.size(), 0);
        const int R=radius;
        for (int r=R; r<I.rows-R; r++)
        {
            for (int c=R; c<I.cols-R; c++)
            {
                uchar v = 0;
                v |= ((I(r  ,c+1) - I(r+R,c+R)) > (I(r  ,c-1) - I(r-R,c-R))) * 1;
                v |= ((I(r+1,c+1) - I(r+R,c  )) > (I(r-1,c-1) - I(r-R,c  ))) * 2;
                v |= ((I(r+1,c  ) - I(r+R,c-R)) > (I(r-1,c  ) - I(r-R,c+R))) * 4;
                v |= ((I(r+1,c-1) - I(r  ,c-R)) > (I(r-1,c+1) - I(r  ,c+R))) * 8;
                fI(r,c) = v;
            }
        }
        features = fI;
        return 16;
    }
};





static void hist_patch(const Mat_<uchar> &fI, Mat &histo, int histSize=256)
{
    Mat_<float> h(1, histSize, 0.0f);
    for (int i=0; i<fI.rows; i++)
    {
        for (int j=0; j<fI.cols; j++)
        {
            int v = int(fI(i,j));
            h( v ) += 1.0f;
        }
    }
    histo.push_back(h.reshape(1,1));
}


static void hist_patch_uniform(const Mat_<uchar> &fI, Mat &histo)
{
    static int uniform[256] =
    {   // the well known original uniform2 pattern
        0,1,2,3,4,58,5,6,7,58,58,58,8,58,9,10,11,58,58,58,58,58,58,58,12,58,58,58,13,58,
        14,15,16,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,17,58,58,58,58,58,58,58,18,
        58,58,58,19,58,20,21,22,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,58,58,58,58,58,58,58,58,58,58,58,23,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,58,24,58,58,58,58,58,58,58,25,58,58,58,26,58,27,28,29,30,58,31,58,58,58,32,58,
        58,58,58,58,58,58,33,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,34,58,58,58,58,
        58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,35,36,37,58,38,58,58,58,39,58,58,58,58,58,58,58,40,58,58,58,58,58,58,58,58,58,
        58,58,58,58,58,58,41,42,43,58,44,58,58,58,45,58,58,58,58,58,58,58,46,47,48,58,49,
        58,58,58,50,51,52,58,53,54,55,56,57
    };

    Mat_<float> h(1, 60, 0.0f); // mod4
    for (int i=0; i<fI.rows; i++)
    {
        for (int j=0; j<fI.cols; j++)
        {
            int v = int(fI(i,j));
            h( uniform[v] ) += 1.0f;
        }
    }
    histo.push_back(h.reshape(1,1));
}


struct GriddedHist
{
    int GRIDX,GRIDY;

    GriddedHist(int gridx=8, int gridy=8)
        : GRIDX(gridx)
        , GRIDY(gridy)
    {}

    void hist(const Mat &feature, Mat &histo, int histSize=256) const
    {
        histo.release();
        int sw = feature.cols/GRIDX;
        int sh = feature.rows/GRIDY;
        for (int i=0; i<GRIDX; i++)
        {
            for (int j=0; j<GRIDY; j++)
            {
                Mat patch(feature, Range(j*sh,(j+1)*sh), Range(i*sw,(i+1)*sw));
                hist_patch(patch, histo, histSize);
            }
        }
        normalize(histo.reshape(1,1),histo);
    }
};


//
// overlapped pyramid of histogram patches
//  (not resizing the feature/image)
//
struct PyramidGrid
{
    bool uniform;

    PyramidGrid(bool uniform=false): uniform(uniform) {}

    void hist_level(const Mat &feature, Mat &histo, int GRIDX, int GRIDY,int histSize=256) const
    {
        int sw = feature.cols/GRIDX;
        int sh = feature.rows/GRIDY;
        for (int i=0; i<GRIDX; i++)
        {
            for (int j=0; j<GRIDY; j++)
            {
                Mat patch(feature, Range(j*sh,(j+1)*sh), Range(i*sw,(i+1)*sw));
                if (uniform && histSize==256)
                    hist_patch_uniform(patch, histo);
                else
                    hist_patch(patch, histo, histSize);
            }
        }
    }

    void hist(const Mat &feature, Mat &histo, int histSize=256) const
    {
        histo.release();
        int levels[] = {5,6,7,8};
        for (int i=0; i<4; i++)
        {
            hist_level(feature,histo,levels[i],levels[i],histSize);
        }
        normalize(histo.reshape(1,1),histo);
    }
};



//
// various attempts to gather landmarks for sampling
//

#ifdef HAVE_DLIB
//
// 20 assorted keypoints extracted from the 68 dlib facial landmarks, based on the
//    Kazemi_One_Millisecond_Face_2014_CVPR_paper.pdf
//
struct LandMarks
{
    dlib::shape_predictor sp;

    LandMarks()
    {   // it's only 95mb...
        dlib::deserialize("data/shape_predictor_68_face_landmarks.dat") >> sp;
    }

    int extract(const Mat &img, vector<Point> &kp) const
    {
        dlib::rectangle rec(0,0,img.cols,img.rows);
        dlib::full_object_detection shape = sp(dlib::cv_image<uchar>(img), rec);

        int idx[] = {17,26, 19,24, 21,22, 36,45, 39,42, 38,43, 31,35, 51,33, 48,54, 57,27, 0};
        //int idx[] = {18,25, 20,24, 21,22, 27,29, 31,35, 38,43, 51, 0};
        for(int k=0; (k<40) && (idx[k]>0); k++)
            kp.push_back(Point(shape.part(idx[k]).x(), shape.part(idx[k]).y()));
        //dlib::point p1 = shape.part(31) + (shape.part(39) - shape.part(31)) * 0.5; // left of nose
        //dlib::point p2 = shape.part(35) + (shape.part(42) - shape.part(35)) * 0.5;
        //dlib::point p3 = shape.part(36) + (shape.part(39) - shape.part(36)) * 0.5; // left eye center
        //dlib::point p4 = shape.part(42) + (shape.part(45) - shape.part(42)) * 0.5; // right eye center
        //dlib::point p5 = shape.part(31) + (shape.part(48) - shape.part(31)) * 0.5; // betw.mouth&nose
        //dlib::point p6 = shape.part(35) + (shape.part(54) - shape.part(35)) * 0.5; // betw.mouth&nose
        //kp.push_back(Point(p1.x(), p1.y()));
        //kp.push_back(Point(p2.x(), p2.y()));
        //kp.push_back(Point(p3.x(), p3.y()));
        //kp.push_back(Point(p4.x(), p4.y()));
        //kp.push_back(Point(p5.x(), p5.y()));
        //kp.push_back(Point(p6.x(), p6.y()));

        return (int)kp.size();
    }
};
#elif 0
struct LandMarks
{
    Ptr<ElasticParts> elastic;

    LandMarks()
    {
        elastic = ElasticParts::createDiscriminative();
        elastic->read("data/disc.xml.gz");
        //elastic = ElasticParts::createGenerative();
        //elastic->read("data/parts.xml.gz");
    }

    int extract(const Mat &img, vector<Point> &kp) const
    {
        elastic->getPoints(img, kp);
        return (int)kp.size();
    }
};
#else
struct LandMarks
{
    int extract(const Mat &img, vector<Point> &kp) const
    {
        kp.push_back(Point(15,19));    kp.push_back(Point(75,19));
        kp.push_back(Point(29,20));    kp.push_back(Point(61,20));
        kp.push_back(Point(36,24));    kp.push_back(Point(54,24));
        kp.push_back(Point(38,35));    kp.push_back(Point(52,35));
        kp.push_back(Point(30,39));    kp.push_back(Point(60,39));
        kp.push_back(Point(19,39));    kp.push_back(Point(71,39));
        kp.push_back(Point(8 ,38));    kp.push_back(Point(82,38));
        kp.push_back(Point(40,64));    kp.push_back(Point(50,64));
        kp.push_back(Point(31,75));    kp.push_back(Point(59,75));
        kp.push_back(Point(27,81));    kp.push_back(Point(63,81));
        if (img.size() != Size(90,90))
        {
            float scale_x=float(img.cols)/90;
            float scale_y=float(img.rows)/90;
            for (size_t i=0; i<kp.size(); i++)
            {
                kp[i].x *= scale_x;
                kp[i].y *= scale_y;
            }
        }
        return (int)kp.size();
    }
};
#endif




//
// 64 hardcoded, precalculated gftt keypoints from the 90x90 cropped mean lfw2 img
//
static void gftt64(const Mat &img, vector<KeyPoint> &kp)
{
    static const int kpsize = 16;
    kp.push_back(KeyPoint(14, 33, kpsize));        kp.push_back(KeyPoint(29, 77, kpsize));        kp.push_back(KeyPoint(55, 60, kpsize));
    kp.push_back(KeyPoint(63, 76, kpsize));        kp.push_back(KeyPoint(76, 32, kpsize));        kp.push_back(KeyPoint(35, 60, kpsize));
    kp.push_back(KeyPoint(69, 21, kpsize));        kp.push_back(KeyPoint(45, 30, kpsize));        kp.push_back(KeyPoint(27, 31, kpsize));
    kp.push_back(KeyPoint(64, 26, kpsize));        kp.push_back(KeyPoint(21, 22, kpsize));        kp.push_back(KeyPoint(25, 27, kpsize));
    kp.push_back(KeyPoint(69, 31, kpsize));        kp.push_back(KeyPoint(54, 81, kpsize));        kp.push_back(KeyPoint(62, 30, kpsize));
    kp.push_back(KeyPoint(20, 32, kpsize));        kp.push_back(KeyPoint(52, 33, kpsize));        kp.push_back(KeyPoint(37, 32, kpsize));
    kp.push_back(KeyPoint(38, 81, kpsize));        kp.push_back(KeyPoint(36, 82, kpsize));        kp.push_back(KeyPoint(32, 31, kpsize));
    kp.push_back(KeyPoint(78, 17, kpsize));        kp.push_back(KeyPoint(59, 24, kpsize));        kp.push_back(KeyPoint(30, 24, kpsize));
    kp.push_back(KeyPoint(11, 18, kpsize));        kp.push_back(KeyPoint(13, 17, kpsize));        kp.push_back(KeyPoint(56, 30, kpsize));
    kp.push_back(KeyPoint(73, 15, kpsize));        kp.push_back(KeyPoint(19, 15, kpsize));        kp.push_back(KeyPoint(57, 53, kpsize));
    kp.push_back(KeyPoint(33, 54, kpsize));        kp.push_back(KeyPoint(34, 52, kpsize));        kp.push_back(KeyPoint(49, 25, kpsize));
    kp.push_back(KeyPoint(66, 33, kpsize));        kp.push_back(KeyPoint(55, 49, kpsize));        kp.push_back(KeyPoint(61, 33, kpsize));
    kp.push_back(KeyPoint(39, 29, kpsize));        kp.push_back(KeyPoint(60, 46, kpsize));        kp.push_back(KeyPoint(40, 26, kpsize));
    kp.push_back(KeyPoint(41, 76, kpsize));        kp.push_back(KeyPoint(50, 76, kpsize));        kp.push_back(KeyPoint(53, 41, kpsize));
    kp.push_back(KeyPoint(44, 23, kpsize));        kp.push_back(KeyPoint(29, 60, kpsize));        kp.push_back(KeyPoint(54, 54, kpsize));
    kp.push_back(KeyPoint(30, 47, kpsize));        kp.push_back(KeyPoint(45, 50, kpsize));        kp.push_back(KeyPoint(83, 35, kpsize));
    kp.push_back(KeyPoint(36, 54, kpsize));        kp.push_back(KeyPoint(13, 46, kpsize));        kp.push_back(KeyPoint(36, 44, kpsize));
    kp.push_back(KeyPoint(83, 38, kpsize));        kp.push_back(KeyPoint(49, 53, kpsize));        kp.push_back(KeyPoint(33, 83, kpsize));
    kp.push_back(KeyPoint(17, 88, kpsize));        kp.push_back(KeyPoint(31, 63, kpsize));        kp.push_back(KeyPoint(13, 27, kpsize));
    kp.push_back(KeyPoint(50, 62, kpsize));        kp.push_back(KeyPoint(11, 43, kpsize));        kp.push_back(KeyPoint(45, 55, kpsize));
    kp.push_back(KeyPoint(45, 56, kpsize));        kp.push_back(KeyPoint(79, 43, kpsize));        kp.push_back(KeyPoint(74, 88, kpsize));
    kp.push_back(KeyPoint(41, 62, kpsize));
    if (img.size() != Size(90,90))
    {
        float scale_x=float(img.cols)/90;
        float scale_y=float(img.rows)/90;
        for (size_t i=0; i<kp.size(); i++)
        {
            kp[i].pt.x *= scale_x;
            kp[i].pt.y *= scale_y;
        }
    }
}

struct GfttGrid
{
    int gr;
    GfttGrid(int gr=4) : gr(gr) {} // 8x8 rect by default

    void hist(const Mat &feature, Mat &histo, int histSize=256) const
    {
        vector<KeyPoint> kp;
        gftt64(feature,kp);
        //gftt96(kp);
        //kp_manual(kp);

        histo.release();
        Rect bounds(Point(),feature.size());
        for (size_t k=0; k<kp.size(); k++)
        {
            Rect part(int(kp[k].pt.x)-gr, int(kp[k].pt.y)-gr, gr*2, gr*2);
            part &= bounds;
            hist_patch(feature(part), histo, histSize);
        }
        normalize(histo.reshape(1,1),histo);
    }
};



//
//
// layered base for lbph,
//  * calc features on the whole image,
//  * calculate the hist on a set of rectangles
//    (which could come from a grid, or a Rects, or a keypoint based model).
//
template <typename Feature, typename Grid>
struct GenericExtractor : public TextureFeature::Extractor
{
    Feature ext;
    Grid grid;

    GenericExtractor(const Feature &ext, const Grid &grid)
        : ext(ext)
        , grid(grid)
    {}

    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat fI;
        int histSize = ext(img, fI);
        grid.hist(fI, features, histSize);
        return features.total() * features.elemSize();
    }
};


//
// instead of adding more bits, concatenate several histograms,
// cslbp + dialbp + sqlbp = 3*16 bins = 12288 feature-bytes.
//
template <typename Grid>
struct CombinedExtractor : public TextureFeature::Extractor
{
    Grid grid;

    CombinedExtractor(const Grid &grid)
        : grid(grid)
    {}

    template <class Extract>
    void extract(const Mat &img, Mat &features, int r) const
    {
        Extract ext(r);
        Mat f,fI;
        int histSize = ext(img, f);
        grid.hist(f, fI, histSize);
        features.push_back(fI.reshape(1,1));
    }
    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        extract<FeatureCsLbp>(img,features,2);
        extract<FeatureCsLbp>(img,features,4);
        extract<FeatureFPLbp>(img,features,2);
        extract<FeatureFPLbp>(img,features,4);
        extract<FeatureDiamondLbp>(img,features,3);
        extract<FeatureSquareLbp>(img,features,4);
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



template <typename Grid>
struct GradMagExtractor : public TextureFeature::Extractor
{
    Grid grid;
    int nbins;

    GradMagExtractor(const Grid &grid)
        : grid(grid)
        , nbins(45)
    {}

    // TextureFeature::Extractor
    virtual int extract(const Mat &I, Mat &features) const
    {
        Mat fgrad, fmag;
        Mat s1, s2, s3(I.size(), CV_32F), s4(I.size(), CV_32F);
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);

        fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        fgrad = s3 / (360/nbins);
        fgrad.convertTo(fgrad,CV_8U);
        Mat fg;
        grid.hist(fgrad,fg,nbins+1);
        features.push_back(fg.reshape(1,1));

        magnitude(s1.ptr<float>(0), s2.ptr<float>(0), s4.ptr<float>(0), I.total());
        normalize(s4,fmag);
        fmag.convertTo(fmag,CV_8U,nbins);
        Mat fm;
        grid.hist(fmag,fm,nbins+1);
        features.push_back(fm.reshape(1,1));

        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};

//
// 2d histogram with "rings" of magnitude and "sectors" of gradients.
//
struct ExtractorGradBin : public TextureFeature::Extractor
{
    int nsec,nrad,grid;
    ExtractorGradBin(int nsec=8, int nrad=2, int grid=18) : nsec(nsec), nrad(nrad), grid(grid) {}

    virtual int extract(const Mat &I, Mat &features) const
    {
        Mat s1, s2, s3(I.size(), CV_32F), s4(I.size(), CV_32F), s5;
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);
        fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        s3 /= (360/nsec);

        magnitude(s1.ptr<float>(0), s2.ptr<float>(0), s4.ptr<float>(0), I.total());
        normalize(s4,s4,nrad);

        int sx = I.cols/(grid-1);
        int sy = I.rows/(grid-1);
        int nbins = nsec*nrad;
        features = Mat(1,nbins*grid*grid,CV_32F,Scalar(0));
        for (int i=0; i<I.rows; i++)
        {
            int oy = i/sy;
            for (int j=0; j<I.cols; j++)
            {
                int ox = j/sx;
                int off = nbins*(oy*grid + ox);
                int g = (int)s3.at<float>(i,j);
                int m = (int)s4.at<float>(i,j);
                features.at<float>(off + g + m*nsec) ++;
            }
        }
        return features.total() * features.elemSize();
    }
};


struct ExtractorGaborGradBin : public ExtractorGradBin
{
    Size kernel_size;

    ExtractorGaborGradBin(int nsec=8, int nrad=2, int grid=12, int kernel_siz=9)
        : ExtractorGradBin(nsec, nrad, grid)
        , kernel_size(kernel_siz, kernel_siz)
    {}

    void gabor(const Mat &src_f, Mat &features,double sigma, double theta, double lambda, double gamma, double psi) const
    {
        Mat dest,dest8u,his;
        cv::filter2D(src_f, dest, CV_32F, getGaborKernel(kernel_size, sigma,theta, lambda, gamma, psi, CV_64F));
        //dest.convertTo(dest8u, CV_8U);
        ExtractorGradBin::extract(dest, his);
        features.push_back(his.reshape(1, 1));
    }

    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat src_f;
        img.convertTo(src_f, CV_32F, 1.0/255.0);
        gabor(src_f, features, 8,4,90,15,0);
        gabor(src_f, features, 8,4,45,30,1);
        gabor(src_f, features, 8,4,45,45,0);
        gabor(src_f, features, 8,4,90,60,1);
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};


//
// concat histograms from lbp-like features generated from a bank of gabor filtered images
//   ( due to memory restrictions, it can't use plain lbp(64k),
//     but has to inherit the 2nd best bed (combined(12k)) )
//
template <typename Grid>
struct ExtractorGabor : public CombinedExtractor<Grid>
{
    Size kernel_size;

    ExtractorGabor(const Grid &grid, int kernel_siz=8)
        : CombinedExtractor<Grid>(grid)
        , kernel_size(kernel_siz, kernel_siz)
    {}

    void gabor(const Mat &src_f, Mat &features,double sigma, double theta, double lambda, double gamma, double psi) const
    {
        Mat dest,dest8u,his;
        cv::filter2D(src_f, dest, CV_32F, getGaborKernel(kernel_size, sigma,theta, lambda, gamma, psi));
        dest.convertTo(dest8u, CV_8U);
        CombinedExtractor<Grid>::extract(dest8u, his);
        features.push_back(his.reshape(1, 1));
    }

    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat src_f;
        img.convertTo(src_f, CV_32F, 1.0/255.0);
        gabor(src_f, features, 8,4,90,15,0);
        gabor(src_f, features, 8,4,45,30,1);
        gabor(src_f, features, 8,4,45,45,0);
        gabor(src_f, features, 8,4,90,60,1);
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



//
// grid it into 8x8 image patches, do a dct on each,
//  concat downsampled 4x4(topleft) result to feature vector.
//
struct ExtractorDct : public TextureFeature::Extractor
{
    int grid;

    ExtractorDct() : grid(8) {}

    virtual int extract( const Mat &img, Mat &features ) const
    {
        Mat src;
        img.convertTo(src,CV_32F,1.0/255.0);
        for(int i=0; i<src.rows-grid; i+=grid)
        {
            for(int j=0; j<src.cols-grid; j+=grid)
            {
                Mat d;
                dct(src(Rect(i,j,grid,grid)),d);
                // downsampling is just a ROI operation here, still we need a clone()
                Mat e = d(Rect(0,0,grid/2,grid/2)).clone();
                features.push_back(e.reshape(1,1));
            }
        }
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



template < class Descriptor >
struct ExtractorGridFeature2d : public TextureFeature::Extractor
{
    int grid;

    ExtractorGridFeature2d(int g=10) : grid(g) {}

    virtual int extract(const Mat &img, Mat &features) const
    {
        float gw = float(img.cols) / grid;
        float gh = float(img.rows) / grid;
        vector<KeyPoint> kp;
        for (float i=gh/2; i<img.rows-gh; i+=gh)
        {
            for (float j=gw/2; j<img.cols-gw; j+=gw)
            {
                KeyPoint k(j, i, gh);
                kp.push_back(k);
            }
        }
        Ptr<Feature2D> f2d = Descriptor::create();
        f2d->compute(img, kp, features);

        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};
typedef ExtractorGridFeature2d<ORB> ExtractorORBGrid;
typedef ExtractorGridFeature2d<BRISK> ExtractorBRISKGrid;
typedef ExtractorGridFeature2d<xfeatures2d::FREAK> ExtractorFREAKGrid;
typedef ExtractorGridFeature2d<xfeatures2d::SIFT> ExtractorSIFTGrid;
typedef ExtractorGridFeature2d<xfeatures2d::BriefDescriptorExtractor> ExtractorBRIEFGrid;

struct ExtractorGfttFeature2d : public TextureFeature::Extractor
{
    Ptr<Feature2D> f2d;
    LandMarks land;

    ExtractorGfttFeature2d(Ptr<Feature2D> f)
        : f2d(f)
    {
    }

    virtual int extract(const Mat &img, Mat &features) const
    {
       // PROFILEX("extract");

        vector<Point> pt;
        land.extract(img,pt);

        size_t s = pt.size();
        float w=5;
        vector<KeyPoint> kp;
        for (size_t i=0; i<s; i++)
        {
            Point2f p(pt[i]);
            kp.push_back(KeyPoint(p.x,p.y,w*2,8));
            kp.push_back(KeyPoint(p.x,p.y-w,w*2,8));
            kp.push_back(KeyPoint(p.x,p.y+w,w*2,8));
            kp.push_back(KeyPoint(p.x-w,p.y,w*2,8));
            kp.push_back(KeyPoint(p.x+w,p.y,w*2,8));
            //kp.push_back(KeyPoint(p.x-w,p.y-w/2,w*2));
            //kp.push_back(KeyPoint(p.x-w,p.y+w/2,w*2));
            //kp.push_back(KeyPoint(p.x+w,p.y-w/2,w*2));
            //kp.push_back(KeyPoint(p.x+w,p.y+w/2,w*2));
        }
        f2d->compute(img, kp, features);
        //Mat f2;
        //for (size_t i=0; i< features.rows; i++)
        //{
        //    Mat f = features.row(i)(Rect(32,0,64,1)).clone().reshape(1,64);
        //    f.push_back(kp[i].pt.x / img.cols - 0.5f);
        //    f.push_back(kp[i].pt.y / img.rows - 0.5f);
        //    f2.push_back(f);
        //}
        // resize(features,features,Size(),0.5,1.0);                  // not good.
        // features = features(Rect(64,0,64,features.rows)).clone();  // amazing.
        //features = features.reshape(1,1);
        normalize(features.reshape(1,1), features);
        return features.total() * features.elemSize();
    }
};



//
// "Review and Implementation of High-Dimensional Local Binary Patterns
//    and Its Application to Face Recognition"
//    Bor-Chun Chen, Chu-Song Chen, Winston Hsu
//
struct HighDimLbp : public TextureFeature::Extractor
{
    FeatureFPLbp lbp;
    LandMarks land;

    virtual int extract(const Mat &img, Mat &features) const
    {
        //PROFILEX("extract");
        int gr=10; // 10 used in paper
        vector<Point> kp;
        land.extract(img,kp);

        Mat histo;
        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
        float offsets_16[] = {
            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
        };
        int noff = 16;
        float *off = offsets_16;
        for (int i=0; i<5; i++)
        {
            float s = scale[i];
            Mat f1,f2,imgs;
            resize(img,imgs,Size(),s,s);
            int histSize = lbp(imgs,f1);

            for (size_t k=0; k<kp.size(); k++)
            {
                Point2f pt(kp[k]);
                for (int o=0; o<noff; o++)
                {
                    Mat patch;
                    getRectSubPix(f1, Size(gr,gr), Point2f(pt.x*s + off[o*2]*gr, pt.y*s + off[o*2+1]*gr), patch);
                    hist_patch(patch, histo, histSize);
                }
            }
        }
        normalize(histo.reshape(1,1), features);
        return features.total() * features.elemSize();
    }
};

struct HighDimLbpPCA : public TextureFeature::Extractor
{
    LandMarks land;
    FeatureFPLbp lbp;
    //FeatureLbp lbp;
    PCA pca[20];

    HighDimLbpPCA()
    {
        FileStorage fs("data/fplbp_pca.xml.gz",FileStorage::READ);
        CV_Assert(fs.isOpened());
        FileNode pnodes = fs["hdlbp"];
        int i=0;
        for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
        {
            pca[i++].read(*it);
        }
        fs.release();

        //FILE * f = fopen("data/pca.hdlbpu.bin","rb");
        //CV_Assert(f!=0);
        //for (int i=0; i<20; i++)
        //{
        //    read_pca(pca[i],f);
        //}
        //fclose(f);
    }
    //void read_pca(PCA &pca, FILE *f)
    //{
    //    int mr,mc;
    //    fread(&mr, sizeof(int), 1, f);
    //    fread(&mc, sizeof(int), 1, f);
    //    pca.mean.create(mr,mc,CV_32F);
    //    fread(pca.mean.ptr<float>(), mc*mr, 1, f);
    //    int er, ec;
    //    fread(&er, sizeof(int), 1, f);
    //    fread(&ec, sizeof(int), 1, f);
    //    pca.eigenvectors.create(er,ec,CV_32F);
    //    fread(pca.eigenvectors.ptr<float>(), 1, ec*er, f);
    //}

    virtual int extract(const Mat &img, Mat &features) const
    {
        //PROFILEX("extract");
        int gr=10; // 10 used in paper
        vector<Point> kp;
        land.extract(img,kp);
        CV_Assert(kp.size()==20);
        Mat histo;
        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
        float offsets_16[] = {
            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
        };
        int noff = 16;
        float *off = offsets_16;
        Mat h[20];
        for (int i=0; i<5; i++)
        {
            float s = scale[i];
            Mat f1,f2,imgs;
            resize(img,imgs,Size(),s,s);
            int histSize = lbp(imgs,f1);

            for (size_t k=0; k<kp.size(); k++)
            {
                Point2f pt(kp[k]);
                for (int o=0; o<noff; o++)
                {
                    Mat patch;
                    getRectSubPix(f1, Size(gr,gr), Point2f(pt.x*s + off[o*2]*gr, pt.y*s + off[o*2+1]*gr), patch);
                    hist_patch(patch, h[k], histSize);
                    //hist_patch_uniform(patch, h[k], histSize);
                }
            }
        }
        for (size_t k=0; k<kp.size(); k++)
        {
            Mat hx = h[k].reshape(1,1);
            normalize(hx,hx);
            Mat hy = pca[k].project(hx);
            histo.push_back(hy);
        }
        normalize(histo.reshape(1,1), features);
        return features.total() * features.elemSize();
    }
};


struct HighDimPCASift : public TextureFeature::Extractor
{
    LandMarks land;
    Ptr<Feature2D> sift;
    PCA pca[20];

    HighDimPCASift()
        : sift(xfeatures2d::SIFT::create())
    {
        FileStorage fs("data/hd_pcasift_20.xml.gz",FileStorage::READ);
        CV_Assert(fs.isOpened());
        FileNode pnodes = fs["hd_pcasift"];
        int i=0;
        for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
        {
            pca[i++].read(*it);
        }
        fs.release();
    }
    virtual int extract(const Mat &img, Mat &features) const
    {
        int gr=5; // 10 used in paper
        vector<Point> pt;
        land.extract(img,pt);
        CV_Assert(pt.size()==20);

        Mat histo;
        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
        float offsets_16[] = {
            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
        };
        float offsets_9[] = {
            -1.f,-1.f, 0.f,-1.f, 1.f,-1.f,
            -1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
            -1.f, 1.f, 0.f, 1.f, 1.f, 1.f,
        };
        int noff = 16;
        float *off = offsets_16;
//        for (int i=0; i<5; i++)
        {
            float s = 1.0f;//scale[i];
            Mat f1,f2,imgs;
            resize(img,imgs,Size(),s,s);

            for (size_t k=0; k<pt.size(); k++)
            {
                vector<KeyPoint> kp;
                Mat h;
                for (int o=0; o<noff; o++)
                {
                    kp.push_back(KeyPoint(pt[k].x*s + off[o*2]*gr, pt[k].y*s + off[o*2+1]*gr,gr));
                }
                sift->compute(imgs,kp,h);
                for (size_t j=0; j<kp.size(); j++)
                {
                    Mat hx = h.row(j).t();
                    hx.push_back(float(kp[j].pt.x/img.cols - 0.5));
                    hx.push_back(float(kp[j].pt.y/img.rows - 0.5));
                    Mat hy = pca[k].project(hx.reshape(1,1));
                    histo.push_back(hy);
                }
            }
        }
        features = histo.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



struct HighDimGrad : public TextureFeature::Extractor
{
    LandMarks land;
    ExtractorGradBin grad;
    PCA pca[20];

    HighDimGrad()
        : grad(18,2,8)
    {
        //FileStorage fs("data/hd_pcagrad.xml.gz",FileStorage::READ);
        //CV_Assert(fs.isOpened());
        //FileNode pnodes = fs["hd_pcasift"];
        //int i=0;
        //for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
        //{
        //    pca[i++].read(*it);
        //}
        //fs.release();
    }
    virtual int extract(const Mat &img, Mat &features) const
    {
        vector<Point> pt;
        land.extract(img,pt);
        CV_Assert(pt.size()==20);

        Mat histo;
        //float scales[] = {1.0f, 1.7f, 2.5f};
        // for (int i=0; i<3; i++)
        {
            //Mat ims;
            //float s=scales[i];
            //resize(img,ims,Size(),s,s);
            for (size_t k=0; k<pt.size(); k++)
            {
                Mat h;
                Mat patch; 
                getRectSubPix(img,Size(32,32),pt[k],patch);
                grad.extract(patch, h);
                histo.push_back(h.reshape(1,1));
                //histo.push_back(pca[k].project(h.reshape(1,1)));
            }
        }
        features = histo.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



//
// CDIKP: A Highly-Compact Local Feature Descriptor  
//        Yun-Ta Tsai, Quan Wang, Suya You 
//
struct ExtractorCDIKP : public TextureFeature::Extractor
{
    LandMarks land;

    template<class T>
    void fast_had(int ndim, int lev, T *in, T *out) const
    {
        int h = lev/2;
        for (int j=0; j<ndim/lev; j++)
        {
            for(int i=0; i<h; i++)
            {
                out[i]   = in[i] + in[i+h];
                out[i+h] = in[i] - in[i+h];
            }
            out += lev;
            in  += lev;
        }
    }
    Mat project(Mat &in) const
    {
        const int keep = 10;
        Mat h = in.clone().reshape(1,1);
        Mat wh(1, h.total(), h.type());
        for (int lev=in.total(); lev>2; lev/=2)
        {
            fast_had(in.total(), lev, h.ptr<float>(), wh.ptr<float>());
            if (lev>4) cv::swap(h,wh);
        }
        return wh(Rect(0,0,keep,1));
    }
    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat fI; 
        img.convertTo(fI,CV_32F);
        Mat dx,dy;
        Sobel(fI,dx,CV_32F,1,0);
        Sobel(fI,dy,CV_32F,0,1);
        const int ps = 16;
        const float step = 3;
        for (float i=ps/4; i<img.rows-3*ps/4; i+=step)
        {
            for (float j=ps/4; j<img.cols-3*ps/4; j+=step)
            {
                Mat patch;
                cv::getRectSubPix(dx,Size(ps,ps),Point2f(j,i),patch);
                features.push_back(project(patch));
                cv::getRectSubPix(dy,Size(ps,ps),Point2f(j,i),patch);
                features.push_back(project(patch));
            }
        }
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};

} // TextureFeatureImpl

namespace TextureFeature
{
using namespace TextureFeatureImpl;

Ptr<Extractor> createExtractor(int extract)
{
    switch(int(extract))
    {
        case EXT_Pixels:   return makePtr< ExtractorPixels >(); break;
        case EXT_Lbp:      return makePtr< GenericExtractor<FeatureLbp,GriddedHist> >(FeatureLbp(), GriddedHist()); break;
        case EXT_LBP_P:    return makePtr< GenericExtractor<FeatureLbp,PyramidGrid> >(FeatureLbp(), PyramidGrid()); break;
        case EXT_LBPU_P:   return makePtr< GenericExtractor<FeatureLbp,PyramidGrid> >(FeatureLbp(), PyramidGrid(true)); break;
        case EXT_TPLbp:    return makePtr< GenericExtractor<FeatureTPLbp,GriddedHist> >(FeatureTPLbp(), GriddedHist()); break;
        case EXT_TPLBP_P:  return makePtr< GenericExtractor<FeatureTPLbp,PyramidGrid> >(FeatureTPLbp(), PyramidGrid()); break;
        case EXT_TPLBP_G:  return makePtr< GenericExtractor<FeatureTPLbp,GfttGrid> >(FeatureTPLbp(), GfttGrid()); break;
        case EXT_FPLbp:    return makePtr< GenericExtractor<FeatureFPLbp,GriddedHist> >(FeatureFPLbp(), GriddedHist()); break;
        case EXT_FPLBP_P:  return makePtr< GenericExtractor<FeatureMTS,PyramidGrid> >(FeatureMTS(), PyramidGrid()); break;
        case EXT_MTS:      return makePtr< GenericExtractor<FeatureMTS,GriddedHist> >(FeatureMTS(), GriddedHist()); break;
        case EXT_MTS_P:    return makePtr< GenericExtractor<FeatureMTS,PyramidGrid> >(FeatureMTS(), PyramidGrid()); break;
        case EXT_BGC1:     return makePtr< GenericExtractor<FeatureBGC1,GriddedHist> >(FeatureBGC1(), GriddedHist()); break;
        case EXT_BGC1_P:   return makePtr< GenericExtractor<FeatureBGC1,PyramidGrid> >(FeatureBGC1(), PyramidGrid()); break;
        case EXT_COMB:     return makePtr< CombinedExtractor<GriddedHist> >(GriddedHist()); break;
        case EXT_COMB_P:   return makePtr< CombinedExtractor<PyramidGrid> >(PyramidGrid()); break;
        case EXT_COMB_G:   return makePtr< CombinedExtractor<GfttGrid> >(GfttGrid()); break;
        case EXT_Dct:      return makePtr< ExtractorDct >(); break;
        case EXT_Orb:      return makePtr< ExtractorORBGrid >();  break;
        case EXT_Sift:     return makePtr< ExtractorSIFTGrid >(20); break;
        case EXT_Sift_G:   return makePtr< ExtractorGfttFeature2d >(xfeatures2d::SIFT::create()); break;
        case EXT_Grad:     return makePtr< GenericExtractor<FeatureGrad,GriddedHist> >(FeatureGrad(),GriddedHist());  break;
        case EXT_Grad_G:   return makePtr< GenericExtractor<FeatureGrad,GfttGrid> >(FeatureGrad(),GfttGrid()); break;
        case EXT_Grad_P:   return makePtr< GenericExtractor<FeatureGrad,PyramidGrid> >(FeatureGrad(),PyramidGrid()); break;
        case EXT_GradMag:  return makePtr< GradMagExtractor<GfttGrid> >(GfttGrid()); break;
        case EXT_GradMag_P:  return makePtr< GradMagExtractor<PyramidGrid> >(PyramidGrid()); break;
        case EXT_GradBin:  return makePtr< ExtractorGradBin >(); break;
        case EXT_GaborLBP: return makePtr< ExtractorGabor<GriddedHist> >(GriddedHist()); break;
        case EXT_GaborGB:  return makePtr< ExtractorGaborGradBin >(); break;
        case EXT_HDGRAD:   return makePtr< HighDimGrad >();  break;
        case EXT_HDLBP:    return makePtr< HighDimLbp >();  break;
        case EXT_HDLBP_PCA:  return makePtr< HighDimLbpPCA >();  break;
        case EXT_PCASIFT:  return makePtr< HighDimPCASift >();  break;
        case EXT_CDIKP:    return makePtr< ExtractorCDIKP >();  break;
        default: cerr << "extraction " << extract << " is not yet supported." << endl; exit(-1);
    }
    return Ptr<Extractor>();
}

} // namespace TextureFeatureImpl

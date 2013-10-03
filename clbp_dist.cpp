#include "SpatialHistogramReco.h"


// here, we try more ideas from :
//
// Xiaoyang Tan and Bill Triggs
// Enhanced Local Texture Feature Sets for Face Recognition Under Difﬁcult Lighting Conditions
//
// instead of collecting uniform lbp or ltp bit features, we restrict it to the 4 bits from Clbp
// but in the very same manner, make a distanceTransform of each bitplane, concat that to the feature vector.
//
// "Description of Interest Regions with Center-Symmetric Local Binary Patterns"
// (http://www.ee.oulu.fi/mvg/files/pdf/pdf_750.pdf).
// 
// (thanks, alberto.fernandez, for correcting the link and hinting at the threshold value!)
//

class ClbpDist : public SpatialHistogramReco
{
protected:

    //! the bread-and-butter thing, collect a histogram (per patch)
    virtual void oper(const Mat & src, Mat & hist) const;

    //! choose a distance function for your algo
    virtual double distance(const Mat & hist_a, Mat & hist_b) const {
        return cv::norm(hist_a,hist_b,NORM_L2SQR); 
    }
private:
    int _numBits;

public:

    ClbpDist( int gridx, int gridy, double threshold = DBL_MAX) 
        : SpatialHistogramReco(gridx,gridy,threshold,0,CV_8U)
        , _numBits(4)
    {}
};



static void pattern(const Mat & src, Mat & lbp) {
    // calculate patterns
    int radius = 1;
    int t = 2; // threshold
    for(int i=radius;i<src.rows-radius;i++) 
    {
        for(int j=radius;j<src.cols-radius;j++) 
        {
            int k = 0;
            //
            // 7 0 1
            // 6 c 2
            // 5 4 3
            //
            // center and ring of neighbours:
            uchar c   = src.at<uchar>(i,j);
            uchar n[8]= {               
                src.at<uchar>(i-1,j),
                src.at<uchar>(i-1,j+1),
                src.at<uchar>(i,j+1),
                src.at<uchar>(i+1,j+1),
                src.at<uchar>(i+1,j),
                src.at<uchar>(i+1,j-1),
                src.at<uchar>(i,j-1),
                src.at<uchar>(i-1,j-1) 
            };
            // save 4 bits ( 1 for each of 4 possible diagonals )
            //  _\|/_
            //   /|\
            // this is the "central symmetric LBP" idea, from :
            // "Description of Interest Regions with Center-Symmetric Local Binary Patterns"
            // (http://www.ee.oulu.fi/mvg/files/pdf/pdf_750.pdf).
            //

            //lbp.at<uchar>(i,j) += (n[0]>n[4])<<k++;
            //lbp.at<uchar>(i,j) += (n[1]>n[5])<<k++;
            //lbp.at<uchar>(i,j) += (n[2]>n[6])<<k++;
            //lbp.at<uchar>(i,j) += (n[3]>n[7])<<k++;

            lbp.at<uchar>(i,j) += (n[0]-n[4]>t)<<k++;
            lbp.at<uchar>(i,j) += (n[1]-n[5]>t)<<k++;
            lbp.at<uchar>(i,j) += (n[2]-n[6]>t)<<k++;
            lbp.at<uchar>(i,j) += (n[3]-n[7]>t)<<k++;
        }
    }
}



void ClbpDist::oper(const Mat & src,Mat & hist) const {
    Size s = src.size();
    Mat lbp = Mat::zeros(s, CV_8U);

    pattern(src,lbp);

    Mat dst,dt;
    for ( int i=0; i<_numBits; i++ ) 
    {
        distanceTransform( (lbp&(1<<i)), dt, DIST_L2, 3 );
        dst.push_back(dt.reshape(0,1));
    }
    hist = dst.reshape(0,1);
}


Ptr<FaceRecognizer> createClbpDistFaceRecognizer(double threshold)
{
    return makePtr<ClbpDist>(8,8,threshold);
}

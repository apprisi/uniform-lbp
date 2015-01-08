//
// run k fold crossvalidation train/test on  person db
//

#include <opencv2/opencv.hpp>
#include <opencv2/core/utility.hpp>
using namespace cv;


#include <iostream>
#include <fstream>
#include <map>
using namespace std;


#include "TextureFeature.h"
#include "Preprocessor.h"


using TextureFeature::Extractor;
using TextureFeature::Reductor;
using TextureFeature::Classifier;

bool debug = false;
RNG rng(getTickCount());

double ct(int64 t)
{
    return double(t) / cv::getTickFrequency();
}


//
// read a 'path <blank> label' list
//
int readtxt(const char *fname, std::vector<std::string> &names, std::vector<int> &labels, size_t maxim)
{
    int maxid=-1;
    std::ifstream in(fname);
    while(in.good() && !in.eof())
    {
        std::string path;
        in >> path;
        names.push_back(path);

        int label;
        in >> label;
        labels.push_back(label);

        maxid=std::max(maxid,label);
        if (labels.size() >= maxim)
            break;
    }
    return maxid;
}


//
// imglists per person.
//  no really, you can't just draw a random probability set from a set of multiple classes and call it a day ...
//
void setupPersons(const vector<int> &labels, vector<vector<int>> &persons)
{
    // find out which index belongs to which person
    //
    persons.resize(1);
    int previd=0;
    for (size_t j=0; j<labels.size(); j++)
    {
        int id = labels[j];
        if (previd!=id)
        {
            persons.push_back(vector<int>());
            previd=id;
        }
        persons.back().push_back(j);
    }
}

int extractDB(const string &txtfile, vector<Mat> &images, Mat &labels, int preproc, int precrop, int maxim, int fixed_size)
{
    // read face db
    vector<string> vec;
    vector<int> vlabels;
    int nsubjects = 1 + readtxt(txtfile.c_str(), vec, vlabels, maxim);

    Preprocessor pre(preproc,precrop,fixed_size);

    //
    // read the images,
    //   correct labels if empty images are skipped
    //   also apply preprocessing,
    //
    int load_flag = preproc==-1 ? 1 : 0;
    for (size_t i=0; i<vec.size(); i++)
    {
        Mat m1 = imread(vec[i], load_flag);
        if (m1.empty())
            continue;

        Mat m2;
        resize(m1, m2, Size(fixed_size, fixed_size));

        Mat m3 = pre.process(m2);

        images.push_back(m3);
        labels.push_back(vlabels[i]);
        //if ( i%33==0) imshow("i",mm), waitKey(0);
    }
    return nsubjects;
}

int crossfoldData(Ptr<Extractor> ext,
                  Ptr<Reductor> red,
                  Mat & trainFeatures,
                  Mat & trainLabels,
                  Mat & testFeatures,
                  Mat & testLabels,
                  const vector<Mat> &images,
                  const vector<int> &labels,
                  const vector<vector<int>> &persons,
                  size_t f, size_t fold)
{
    int fsiz=0;

    // split train/test set per person:
    for (size_t j=0; j<persons.size(); j++)
    {
        size_t n_per_person = persons[j].size();
        if (n_per_person < fold)
            continue;
        int r = (fold != 0) ? (n_per_person/fold) : -1;
        for (size_t n=0; n<n_per_person; n++)
        {
            int index = persons[j][n];

            Mat feature;
            ext->extract(images[index],feature);

            if (!red.empty())
            {
                red->reduce(feature, feature);
            }

            fsiz = feature.total() * feature.elemSize();

            // sliding window per fold
            if ((fold>1) && (n >= f*r) && (n <= (f+1)*r))
            {
                testFeatures.push_back(feature);
                testLabels.push_back(labels[index]);
            }
            else
            {
                trainFeatures.push_back(feature);
                trainLabels.push_back(labels[index]);
            }
        }
    }
    return fsiz;
}


double runtest(string name, Ptr<Extractor> ext, Ptr<Reductor> red, Ptr<Classifier> cls, const vector<Mat> &images, const vector<int> &labels, const vector<vector<int>> &persons, size_t fold=10)
{
    //
    // for each fold, take alternating n/fold items for test, the others for training
    //
    // each test is confused on its own over a lot of folds..
    Mat confusion = Mat::zeros(persons.size(),persons.size(),CV_32F);

    int64 t0=getTickCount();
    int fsiz=0;
    for (size_t f=0; f<fold; f++)
    {
        int64 t1 = cv::getTickCount();
        Mat trainFeatures, trainLabels;
        Mat testFeatures,  testLabels;

        fsiz = crossfoldData(ext,red,trainFeatures,trainLabels,testFeatures,testLabels,images,labels,persons,f,fold);
        trainFeatures = trainFeatures.reshape(1,trainLabels.rows);

        cls->train(trainFeatures,trainLabels);

        Mat conf = Mat::zeros(confusion.size(), CV_32F);
        for (int i=0; i<testFeatures.rows; i++)
        {
            Mat res;
            Mat feat = testFeatures.row(i);
            cls->predict(feat.reshape(1,1), res);

            int pred = int(res.at<float>(0));
            int ground = testLabels.at<int>(i);
            if (pred<0 || ground<0)
            {
                cerr << "neg prediction " << f << " " << i << " " << pred << " " << ground << endl;
                continue;
            }
            conf.at<float>(ground, pred) ++;
        }
        confusion += conf;

        double all = sum(confusion)[0];
        double neg = all - sum(confusion.diag())[0];
        double err = double(neg)/all;
        cout << format("%-23s %-2d %6d %6d %6d %8.3f",name.c_str(),(f+1), fsiz, int(all-neg), int(neg), (1.0-err)) << '\r';
    }


    // evaluate. this is probably all too simple.
    double all = sum(confusion)[0];
    double neg = all - sum(confusion.diag())[0];
    double err = double(neg)/all;
    int64 t1=getTickCount() - t0;
    double t(t1/getTickFrequency());
    cout << format("%-26s %6d %6d %6d %8.3f %8.3f",name.c_str(), fsiz, int(all-neg), int(neg), (1.0-err), t) << endl;
    if (debug) cout << "confusion" << endl << confusion(Range(0,min(20,confusion.rows)), Range(0,min(20,confusion.cols))) << endl;
    return err;
}

double runtest(int ext, int red, int cls, const vector<Mat> &images, const vector<int> &labels, const vector<vector<int>> &persons, size_t fold=10)
{
    string name = format( "%s.%s.%s", TextureFeature::EXS[ext], TextureFeature::REDS[red], TextureFeature::CLS[cls]); 
    try
    {
        runtest(name,  
            TextureFeature::createExtractor(ext),  
            TextureFeature::createReductor(red),
            TextureFeature::createClassifier(cls),
            images,labels,persons, fold); 
    }
    catch(...) 
    {
        cerr << name << " crashed.." << endl;
    }
    
}

void printOptions()
{
    cerr << "[extractors]  :"<< endl;
    for (size_t i=0; i<TextureFeature::EXT_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::EXS[i],i); }
    cerr << endl << endl << "[reductors] :" << endl;
    for (size_t i=0; i<TextureFeature::RED_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::REDS[i],i); }
    cerr << endl << endl << "[classifiers] :" << endl;
    for (size_t i=0; i<TextureFeature::CL_MAX; ++i)  {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::CLS[i],i);  }
    //cerr << endl << endl <<  "[preproc] :" << endl;
    //for (size_t i=0; i<TextureFeature::PRE_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::PPS[i],i);  }
    cerr << endl;
}



int main(int argc, const char *argv[])
{
    vector<Mat> images;
    Mat labels;


    const char *keys =
            "{ help h usage ? |           | show this message }"
            "{ opts o         |           | show extractor / reducer/ classifier options }"
            "{ path p         |senthil.txt| path to dataset  }"
            "{ fold f         |10         | folds for crossvalidation }"
            "{ ext e          |0          | extractor enum }"
            "{ red r          |0          | reductor enum }"
            "{ cls c          |0          | classifier enum }"
            "{ all a          |false      | test all }"
            "{ pre P          |0          | preprocessing }"
            "{ crop C         |0          | crop outer pixels }";
 
    CommandLineParser parser(argc, argv, keys);
    string path(parser.get<string>("path"));
    if (parser.has("help") || path=="true")
    {
        parser.printMessage();
        return -1;
    }
    if (parser.has("opts"))
    {
        printOptions();
        return -1;
    }
    int all = parser.has("all");
    int ext = parser.get<int>("ext");
    int red = parser.get<int>("red");
    int cls = parser.get<int>("cls");
    int pre = parser.get<int>("pre");
    int crp = parser.get<int>("crop");
    int fold = parser.get<int>("fold");

    std::string db_path = parser.get<String>("path");

    extractDB(db_path, images, labels, pre, crp, 500, 90);

    // per person id lookup
    vector<vector<int>> persons;
    setupPersons( labels, persons );
    fold = std::min(fold,int(images.size()/persons.size()));

    // some diagnostics:
    String dbs = db_path.substr(0,db_path.find_last_of('.')) + ":";
    char *pp[] = { "no preproc","eqhist","clahe","retina","tan-triggs","crop",0 };
    if (all)
        cout << "-------------------------------------------------------------------" << endl;
    cout << format("%-26s",dbs.c_str()) << fold  << " fold, " << persons.size()  << " classes, " << images.size() << " images, " << pp[pre] << endl;
    if (all)
    {
        cout << "-------------------------------------------------------------------" << endl;
        cout << "[method]                 [f_bytes]  [hit]  [miss]  [acc]   [time]  " << endl;
    }

    if ( ! all )
    {
        runtest(ext,red,cls,images,labels,persons, fold);
    }
    else
    {
        int tests[] = {
            TextureFeature::EXT_Pixels, TextureFeature::RED_NONE, TextureFeature::CL_NORM_L2,
            TextureFeature::EXT_Dct,    TextureFeature::RED_NONE, TextureFeature::CL_COSINE,
            TextureFeature::EXT_Lbp,    TextureFeature::RED_NONE, TextureFeature::CL_HIST_HELL,
            TextureFeature::EXT_Lbp,    TextureFeature::RED_NONE, TextureFeature::CL_SVM_POL,
            TextureFeature::EXT_Lbp,    TextureFeature::RED_WHAD, TextureFeature::CL_SVM_HEL,
            TextureFeature::EXT_Lbp,    TextureFeature::RED_DCT8, TextureFeature::CL_PCA_LDA,
            TextureFeature::EXT_MTS_P,  TextureFeature::RED_DCT8, TextureFeature::CL_PCA_LDA,
            TextureFeature::EXT_COMB_G, TextureFeature::RED_NONE, TextureFeature::CL_PCA_LDA,
            -1,-1,-1
        };
        for (int i=0; tests[i]>-1; i+=3)
            runtest(tests[i], tests[i+1], tests[i+2], images, labels, persons, fold);
    }
    return 0;
}




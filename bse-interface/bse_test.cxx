#include <iostream>
#include <getopt.h>
#include <cmath>
#include <vector>
#include <string>
#include "bse_interface.h"
#include "../src/io.hpp"

struct BinaryBase{
    double m1,m2,period,ecc;
    double tphys;
    int binary_type;
    StarParameter star[2];
    StarParameterOut out[2];

    void readAscii(FILE* fp) {
        int rcount=fscanf(fp, "%lf %lf %lf %lf",
                          &m1, &m2, &period, &ecc);
        if(rcount<4) {
            std::cerr<<"Error: Data reading fails! requiring data number is 4, only obtain "<<rcount<<".\n";
            abort();
        }
    }
};

int main(int argc, char** argv){

    int arg_label;
    int width=20;
    int n=5000;
    double m_min=0.08, m_max=150.0;
    double time=100.0;
    double dtmin=1.0;
    std::vector<double> mass0;
    std::vector<BinaryBase> bin;

    auto printHelp= [&]() {
        std::cout<<"BSE_test [options] [initial mass of stars, can be multiple values]\n"
                 <<"  If no initial mass or no binary table (-b) is provided, N single stars (-n) with equal mass interal in Log scale will be evolved\n"
                 <<"  The default unit set is: Msun, Myr. If input data have different units, please modify the scaling fators\n"
                 <<"    -s [D]: mimimum mass ("<<m_min<<")\n"
                 <<"    -e [D]: maximum mass ("<<m_max<<")\n"
                 <<"    -n [I]: number of stars when evolve an IMF ("<<n<<")\n"
                 <<"    -t [D]: evolve time ("<<time<<")[Myr]\n"
                 <<"    -d [D]: minimum time step ("<<dtmin<<")[Myr]\n"
                 <<"    -b [S]: a file of binary table: First line: number of binary; After: m1, m2, period, ecc per line\n"
                 <<"    -w [I]: print column width ("<<width<<")\n"
                 <<"    -h    : help\n";
    };

    IOParamsBSE bse_io;
    opterr = 0;
    bse_io.print_flag = true;
    int opt_used = bse_io.read(argc,argv);

    // reset optind
    optind=1;
    static struct option long_options[] = {{0,0,0,0}};

    std::string fbin_name;

    int option_index;
    while ((arg_label = getopt_long(argc, argv, "s:e:n:t:d:b:w:h", long_options, &option_index)) != -1)
        switch (arg_label) {
        case 's':
            m_min = atof(optarg);
            std::cout<<"min mass: "<<m_min<<std::endl;
            break;
        case 'e':
            m_max = atof(optarg);
            std::cout<<"max mass: "<<m_max<<std::endl;
            break;
        case 'n':
            n = atof(optarg);
            std::cout<<"N: "<<n<<std::endl;
            break;
        case 't':
            time = atof(optarg);
            std::cout<<"finish time[Myr]: "<<time<<std::endl;
            break;
        case 'w':
            width = atoi(optarg);
            std::cout<<"print width: "<<width<<std::endl;
            break;
        case 'd':
            dtmin = atof(optarg);
            std::cout<<"minimum time step "<<dtmin<<std::endl;
            break;
        case 'b':
            fbin_name = optarg;
            break;
        case 'h':
            printHelp();
            return 0;
        case '?':
            opt_used--;
            break;
        default:
            break;
        }        

    if (fbin_name!="") {
        FILE* fbin;
        if( (fbin = fopen(fbin_name.c_str(),"r")) == NULL) {
            fprintf(stderr,"Error: Cannot open file %s.\n", fbin_name.c_str());
            abort();
        }
        int nb;
        int rcount=fscanf(fbin, "%d", &nb);
        if(rcount<1) {
            std::cerr<<"Error: Data reading fails! requiring data number is 1, only obtain "<<rcount<<".\n";
            abort();
        }
        for (int k=0; k<nb; k++) {
            BinaryBase bink;
            bink.readAscii(fbin);
            bin.push_back(bink);
        }
    }

    // argc and optind are 1 when no input is given
    opt_used += optind;
    // read initial mass list
    bool read_mass_flag = false;
    if (opt_used<argc) {
        while (opt_used<argc) 
            mass0.push_back(atof(argv[opt_used++]));
        read_mass_flag = true;
    }

    BSEManager bse_manager;

    bse_manager.initial(bse_io,true);

    assert(bse_manager.checkParams());

    // first check whether binary exist
    if (bin.size()>0) {
        int nbin = bin.size();
#pragma omp parallel for schedule(dynamic)
        for (int i=0; i<nbin; i++) {
            // initial
            bin[i].star[0].initial(bin[i].m1*bse_manager.mscale);
            bin[i].star[1].initial(bin[i].m2*bse_manager.mscale);
            bin[i].tphys = 0.0;
            bin[i].binary_type = 0;

            bool kick_print_flag[2]={false,false};
            // evolve
            while (bse_manager.getTime(bin[i].star[0])<time) {
                // time step
                double dt1 = bse_manager.getTimeStep(bin[i].star[0]);
                double dt2 = bse_manager.getTimeStep(bin[i].star[1]);
                double dt = std::min(dt1,dt2);
                dt = std::max(dt,dtmin);
                dt = std::min(time-bse_manager.getTime(bin[i].star[0]), dt);

                // evolve function
                bin[i].binary_type = 0;
                int error_flag=bse_manager.evolveBinary(bin[i].star[0],bin[i].star[1],bin[i].out[0],bin[i].out[1],bin[i].period,bin[i].ecc,bin[i].binary_type, dt);
                if (bin[i].binary_type) {
                    std::cout<<bse_manager.binary_type[bin[i].binary_type]
                             <<" i="<<i<<" period[in]="<<bin[i].period<<" ecc="<<bin[i].ecc
                             <<std::endl;
                    std::cout<<"Star 1:";
                    bin[i].star[0].print(std::cout);
                    std::cout<<"\nStar 2:";
                    bin[i].star[1].print(std::cout);
                    std::cout<<std::endl;
                }
                for (int k=0; k<2; k++) {
                    double dv[4];
                    dv[3] = bse_manager.getVelocityChange(dv,bin[i].out[k]);
                    if (dv[3]>0&&!kick_print_flag[k]) {
                        std::cout<<"SN kick, i="<<i<<" vkick[IN]="<<dv[3]<<" ";
                        bin[i].star[k].print(std::cout);
                        std::cout<<std::endl;
                        kick_print_flag[k]=true;
                    }
                }
                if (error_flag) {
                    std::cerr<<"Error: i="<<i<<" mass0[IN]="<<bin[i].m1<<" "<<bin[i].m2<<" period[IN]="<<bin[i].period<<" ecc[IN]="<<bin[i].ecc
                             <<std::endl;
                    std::cerr<<"Star 1:";
                    bin[i].star[0].print(std::cerr);
                    std::cerr<<"\nStar 2:";
                    bin[i].star[1].print(std::cerr);
                    std::cerr<<std::endl;
                    std::cerr<<std::endl;
                }
                double dt_miss = bse_manager.getDTMiss(bin[i].out[0]);
                if (dt_miss!=0.0&&bin[i].star[0].kw>=15&&bin[i].star[1].kw>=15) break;
            }
        }

        std::cout<<std::setw(width)<<"Mass_init1[Msun]"
                 <<std::setw(width)<<"Mass_init2[Msun]"
                 <<std::setw(width)<<"Period[days]"
                 <<std::setw(width)<<"Eccentricty";
        StarParameter::printColumnTitle(std::cout, width);
        StarParameterOut::printColumnTitle(std::cout, width);
        StarParameter::printColumnTitle(std::cout, width);
        StarParameterOut::printColumnTitle(std::cout, width);
        std::cout<<std::endl;

        for (int i=0; i<nbin; i++) {
            std::cout<<std::setw(width)<<bin[i].m1*bse_manager.mscale;
            std::cout<<std::setw(width)<<bin[i].m2*bse_manager.mscale;
            std::cout<<std::setw(width)<<bin[i].period*bse_manager.mscale*3.6524e8;
            std::cout<<std::setw(width)<<bin[i].ecc;
            for (int k=0; k<2; k++) {
                bin[i].star[k].printColumn(std::cout, width);
                bin[i].out[k].printColumn(std::cout, width);
            }
            std::cout<<std::endl;
        }
    }

    // if no mass is read, use a mass range
    if (!read_mass_flag&&bin.size()==0) {
        double dm_factor = exp((log(m_max) - log(m_min))/n);

        mass0.push_back(m_min);
        for (int i=1; i<n; i++) {
            mass0.push_back(mass0.back()*dm_factor);
        }
    }
    else n = mass0.size();

    if (n>0) {
        StarParameter star[n];
        StarParameterOut output[n];

        // initial parameter
        for (int i=0; i<n; i++) {
            star[i].initial(mass0[i]/bse_manager.mscale);
        }

#pragma omp parallel for schedule(dynamic)
        for (int i=0; i<n; i++) {
            //int error_flag = bse_manager.evolveStar(star[i],output[i],time);
            bool kick_print_flag=false;
            while (bse_manager.getTime(star[i])<time) {
                double dt = std::max(bse_manager.getTimeStep(star[i]),dtmin);
                dt = std::min(time-bse_manager.getTime(star[i]), dt);
                int error_flag=bse_manager.evolveStar(star[i],output[i],dt);
                double dv[4];
                dv[3] = bse_manager.getVelocityChange(dv, output[i]);
                if (dv[3]>0&&!kick_print_flag) {
                    std::cout<<"SN kick, i="<<i<<" vkick[IN]="<<dv[3]<<" ";
                    star[i].print(std::cout);
                    std::cout<<std::endl;
                    kick_print_flag=true;
                }
                if (error_flag) {
                    std::cerr<<"Error: i="<<i<<" mass0[IN]="<<mass0[i]<<" ";
                    star[i].print(std::cerr);
                    std::cerr<<std::endl;
                }
                double dt_miss = bse_manager.getDTMiss(output[i]);
                if (dt_miss!=0.0&&star[i].kw>=15) break;
            }
        }

        std::cout<<std::setw(width)<<"Mass_init[Msun]";
        StarParameter::printColumnTitle(std::cout, width);
        StarParameterOut::printColumnTitle(std::cout, width);
        std::cout<<std::endl;

        for (int i=0; i<n; i++) {
            std::cout<<std::setw(width)<<mass0[i]*bse_manager.mscale;
            star[i].printColumn(std::cout, width);
            output[i].printColumn(std::cout, width);
            std::cout<<std::endl;
        }
    }

    return 0;
}

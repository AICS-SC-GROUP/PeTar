#pragma once

#include"search_group.hpp"
#include "stability.hpp"

//! Group paramter class
/* get artifical particle group parameters
 */
class GroupPars{
public:
    PS::S32 id;         ///> group id corresponding to the first member
    PS::S32 i_cluster;  ///> cluster index
    PS::S32 i_group;    ///> group index in cluster
    PS::S32 n_members;  ///> number of members
    PS::S32 n_members_1st; ///> number of members in first component
    PS::S32 n_members_2nd; ///> number of members in second component
    PS::S32 offset_cm;   ///> c.m. index offset in group
    PS::S32 offset_orb;  ///> orbital partical offset in group
    PS::S32 offset_tt;   ///> tital tensor partical offset in group
    PS::S32 n_ptcl_artifical; ///> artifical particle number

    GroupPars(const PS::S32 _n_split): id(-10), i_cluster(-1), i_group(-1), n_members(0), n_members_1st(0), n_members_2nd(0), offset_cm(2*_n_split), 
                                       offset_orb(8), 
                                       offset_tt(0), n_ptcl_artifical(2*_n_split+1) {}
    GroupPars() {init(0);}

    void init(const PS::S32 _n_split) {
        id=-10;
        i_cluster=-1;
        i_group=-1;
        n_members=0;
        n_members_1st=0;
        n_members_2nd=0;
        offset_cm=2*_n_split;
        offset_orb=8;
        offset_tt=0;
        n_ptcl_artifical=2*_n_split+1;
    }

    //! return group parameters
    /* @param[in] _ptcl_artifical: ptcl artifical particle group
    */
    template <class Tptcl>
    void getGroupIndex(Tptcl* _ptcl_artifical) {
        n_members_1st = _ptcl_artifical[0].status.d;
        n_members_2nd = _ptcl_artifical[1].status.d;
        i_cluster = _ptcl_artifical[2].status.d-1;
        i_group   = _ptcl_artifical[3].status.d-1;
        n_members = _ptcl_artifical[offset_cm].status.d;
        id        =-_ptcl_artifical[offset_cm].id;
    }
    
};

template <class Tptcl>
class FakeParticleManager{

    struct BinPar {
        Tptcl* adr_ref; PS::S32* group_list; PS::S32 n; ChangeOver* changeover;
    };

    // set binary tree parameters 
    static PS::S64 setBinChangeOverIDAndGetMemberAdrIter(BinPar& _par, const PS::S64& _id1, const PS::S64& _id2, COMM::BinaryTree<Tptcl>& _bin) {
        // set bin id as the left member id
        if (_id1<0) _bin.id = _bin.getLeftMember()->id;
        else _bin.id = _id1;
#ifdef FAKE_PARTICLE_DEBUG
        assert(_bin.id>0);
#endif

        // leaf case
        if (_id1<0  && _id2<0) {
            // collect address
            for (int k=0; k<2; k++) {
                Tptcl* member = _bin.getMember(k);
#ifdef FAKE_PARTICLE_DEBUG
                assert(member->mass>0.0);
#endif                
                _par.group_list[_par.n++] = member - _par.adr_ref;
                member->status.d = 1; // used for number count later
            }
        }

        //set changeover to the same (root) one
        _bin.changeover = *(_par.changeover);
        
        return _bin.id;
    }

    // get new changeover and rsearch for c.m.
    template <class Tchp>
    static Tchp* calcBinChangeOverAndRSearchIter (Tchp*& _ch, COMM::BinaryTree<Tptcl>& _bin) {
        _bin.changeover.setR(_bin.mass*_ch->mean_mass_inv, _ch->rin, _ch->rout);
        _bin.Ptcl::calcRSearch(_ch->dt_tree);
        return _ch;
    };

    //! generate kepler sampling artifical particles
    /*  @param[in]     _i_cluster: cluster index
        @param[in]     _i_group: group index in the cluster
        @param[in,out] _ptcl_in_cluster: particle data in local cluster
        @param[out]    _ptcl_new: artifical particles that will be added
        @param[in,out] _empty_list: the list of _ptcl_in_cluster that can be used to store new artifical particles, reduced when used
        @param[out]    _group_ptcl_adr_list: group member particle index list in _ptcl_in_cluster 
        @param[in]     _bin: binary tree root
        @param[in]     _id_offset: for artifical particles, the offset of starting id.
        @param[in]     _n_split: split number for artifical particles

        also store the binary parameters in mass_bk of first 4 pairs of artifical particles
        acc, ecc
        peri, tstep (integrator step estimation ),
        inc, OMG,
        omg, ecca
        tperi, stable_factor 
        mass1, mass2

        first two artifical particles status is n_members of two components.
     */
    void keplerOrbitGenerator(const PS::S32 _i_cluster,
                              const PS::S32 _i_group,
                              Tptcl* _ptcl_in_cluster,
                              PS::ReallocatableArray<Tptcl> & _ptcl_new,
                              PS::ReallocatableArray<PS::S32> & _empty_list,
                              PS::S32 *_group_ptcl_adr_list,
                              COMM::BinaryTree<Tptcl> &_bin,
                              const PS::F64 _r_bin,
                              const PS::S64 _id_offset,
                              const PS::S32 _n_split) {
        const PS::F64 dE = 8.0*atan(1.0)/(_n_split-4);
//      const PS::F64 dE = 8.0*atan(1.0)/_n_split;
        //const PS::F64 dt = _bin.peri/_n_split;
        if (_n_split<8) {
            std::cerr<<"N_SPLIT "<<_n_split<<" to small to save binary parameters, should be >= 8!";
            abort();
        }
        PS::S32 i_cg[2]={_i_cluster, _i_group};
        PS::F64* pm[_n_split][2];
        PS::F64 mfactor;
        PS::F64 mnormal=0.0;

        const PS::S32 n_members = _bin.getMemberN();
        //! Set member particle status=1, return orderd particle member index list
        /*  _adr_ref: ptcl_org first particle address as reference to calculate the particle index.
        */
        BinPar bin_par = {_ptcl_in_cluster, _group_ptcl_adr_list, 0, &_bin.changeover};
        _bin.processTreeIter(bin_par, (PS::S64)-1, (PS::S64)-1, setBinChangeOverIDAndGetMemberAdrIter);
#ifdef FAKE_PARTICLE_DEBUG
        assert(bin_par.n==n_members);
#endif                

        // Make sure the _ptcl_new will not make new array due to none enough capacity during the following loop, otherwise the p[j] pointer will point to wrong position
        _ptcl_new.reserveEmptyAreaAtLeast(2*_n_split+1-_empty_list.size());
        // First 4 is used for tidal tensor points
        // remaining is used for sample points
        for (int i=0; i<_n_split; i++) {
            Tptcl* p[2];
            for(int j=0; j<2; j++) {
                if(_empty_list.size()>0) {
                    PS::S32 k = _empty_list.back();
                    _empty_list.decreaseSize(1);
                    p[j] = &_ptcl_in_cluster[k];
                }
                else {
                    _ptcl_new.push_back(Tptcl());
                    p[j] = &_ptcl_new.back();
                }
                Tptcl* member = _bin.getMember(j);
                p[j]->mass = member->mass;
#ifdef FAKE_PARTICLE_DEBUG
                assert(member->mass>0);
#endif
                p[j]->id = _id_offset + abs(member->id)*_n_split + i;
                if(i==0) p[j]->status.d = _bin.isMemberTree(j) ? ((COMM::BinaryTree<Tptcl>*)member)->getMemberN() : 1; // store the component member number 
                else if(i==1) p[j]->status.d = i_cg[j]+1; // store the i_cluster and i_group for identify artifical particles, +1 to avoid 0 value (status.d>0)
                else p[j]->status.d = (_bin.id<<ID_PHASE_SHIFT)|i; // not used, but make status.d>0
                
                if(i>=4) 
                    pm[i][j] = &(p[j]->mass);
            }
            if (i>=4) {
                PS::S32 iph = i-4;

                for (int j=0; j<2; j++) {
                    // use member changeover, if new changeover is different, record the scale ratio 
                    p[j]->changeover =  _bin.getMember(j)->changeover;
                    if (abs(p[j]->changeover.getRin()-_bin.changeover.getRin())>1e-10) {
                        p[j]->changeover.r_scale_next = _bin.changeover.getRin()/p[j]->changeover.getRin();
                        p[j]->r_search = std::max(p[j]->r_search, _bin.r_search);
#ifdef FAKE_PARTICLE_DEBUG
                        assert(p[j]->r_search > p[j]->changeover.getRout());
#endif 
                    }
                    else p[j]->r_search = _bin.r_search;
                }

                // center_of_mass_shift(*(Tptcl*)&_bin,p,2);
                // generate particles at different orbitial phase
                _bin.orbitToParticle(*p[0], *p[1], _bin, dE*iph, G);
                //DriveKeplerOrbParam(p[0]->pos, p[1]->pos, p[0]->vel, p[1]->vel, p[0]->mass, p[1]->mass, (i+1)*dt, _bin.semi, _bin.ecc, _bin.inc, _bin.OMG, _bin.omg, _bin.peri, _bin.ecca);
            }
            else {
                // use c.m. r_search 
                for (int j=0; j<2; j++) {
                    p[j]->r_search =_bin.getMember(j)->r_search;
                    p[j]->changeover = _bin.changeover;
                }

                ///* Assume apo-center distance is the maximum length inside box
                //   Then the lscale=apo/(2*sqrt(2))
                // */
                // PS::F64 lscale = _bin.semi*(1+_bin.ecc)*0.35;

                // Use fixed 0.5*r_bin to determine lscale
                PS::F64 lscale = 0.16*_r_bin;

                // 8 points box 
                switch(i) {
                case 0:
                    p[0]->pos = PS::F64vec(lscale, 0,      -lscale) + _bin.pos;
                    p[1]->pos = PS::F64vec(0,      lscale, -lscale) + _bin.pos;
                    break;
                case 1:
                    p[0]->pos = PS::F64vec(-lscale, 0,      -lscale) + _bin.pos;
                    p[1]->pos = PS::F64vec(0,      -lscale, -lscale) + _bin.pos;
                    break;
                case 2:
                    p[0]->pos = PS::F64vec(lscale, 0,      lscale) + _bin.pos;
                    p[1]->pos = PS::F64vec(0,      lscale, lscale) + _bin.pos;
                    break;
                case 3:
                    p[0]->pos = PS::F64vec(-lscale, 0,      lscale) + _bin.pos;
                    p[1]->pos = PS::F64vec(0,      -lscale, lscale) + _bin.pos;
                    break;
                default:
                    std::cerr<<"Error: index >= 4!\n";
                    abort();
                }
                p[0]->vel = _bin.vel;
                p[1]->vel = _bin.vel;
                p[0]->mass = p[1]->mass = 0.0;
            }
            // binary parameters
            if(i==5) {
                p[0]->mass_bk.d = p[0]->mass;
                p[1]->mass_bk.d = p[1]->mass;
            }
            else if(i==6) {
                p[0]->mass_bk.d = 0.0; // indicate the order
                p[1]->mass_bk.d = 1.0;
            }
            if(i>=4) {
                PS::F64vec dvvec= p[0]->vel - p[1]->vel;
                PS::F64 odv = 1.0/std::sqrt(dvvec*dvvec);
                for(int j=0; j<2; j++) p[j]->mass *= odv;
                //if(i==0) p[j]->mass /= 2.0*_n_split;
                //else p[j]->mass /= _n_split;
                mnormal += odv;
                // center_of_mass_correction 
                for (int j=0; j<2; j++) {
                    p[j]->pos += _bin.pos;
                    p[j]->vel += _bin.vel;
                }
#ifdef FAKE_PARTICLE_DEBUG
                //check rsearch consistence:
                PS::F64 rsearch_bin = _bin.r_search+_bin.semi*(1+_bin.ecc);
                for(int j=0; j<2; j++) {
                    PS::F64vec dp = p[j]->pos-_bin.pos;
                    PS::F64 dr = dp*dp;
                    assert(dr<=rsearch_bin*rsearch_bin);
//                    if(p[j]->id==10477) {
//                        std::cerr<<"i="<<i<<" dr="<<sqrt(dr)<<std::endl;
//                        p[j]->print(std::cerr);
//                        std::cerr<<std::endl;
//                    }
                }
#endif
            }
        }

        mfactor = 1.0/mnormal;

        for (int i=4; i<_n_split; i++) 
            for (int j=0; j<2; j++) 
                *pm[i][j] *= mfactor;
        
        //mfactor *= 0.5;
        //*pm[0][0] *= mfactor;
        //*pm[0][1] *= mfactor;
        //mfactor /= _n_split;
        // collect the member address .

        Tptcl* pcm;
        //PS::S64 pcm_adr;
        if(_empty_list.size()>0) {
            pcm = &_ptcl_in_cluster[_empty_list.back()];
            //pcm_adr = - _empty_list.back(); // if is in empty list, put negative address
            _empty_list.decreaseSize(1);
        }
        else {
            _ptcl_new.push_back(Tptcl());
            pcm = &_ptcl_new.back();
            //pcm_adr = _ptcl_new.size()-1; // if is in new, put ptcl_new address
        }
        pcm->mass_bk.d = _bin.mass;
        pcm->mass = 0.0;
        pcm->pos = _bin.pos;
        pcm->vel = _bin.vel;
        pcm->id  = - std::abs(_bin.id);
        pcm->r_search = _bin.r_search;
        pcm->changeover = _bin.changeover;

        pcm->r_search += _bin.semi*(1+_bin.ecc);  // depend on the mass ratio, the upper limit distance to c.m. from all members and artifical particles is apo-center distance

        pcm->status.d = _bin.getMemberN();
    }


public:
    PS::F64 G; // gravitational constant
      
    //! generate artifical particles,
    /*  @param[in]     _i_cluster: cluster index
        @param[in,out] _ptcl_in_cluster: particle data
        @param[in]     _n_ptcl: total number of particle in _ptcl_in_cluster.
        @param[out]    _ptcl_artifical: artifical particles that will be added
        @param[out]    _n_groups: number of groups in current cluster
        @param[in,out] _groups: searchgroup class, which contain 1-D group member index array, will be reordered by the minimum distance chain for each group
        @param[in,out] _empty_list: the list of _ptcl_in_cluster that can be used to store new artifical particles, reduced when used
        @param[in]     _rbin: binary detection criterion radius
        @param[in]     _rin: inner radius of soft-hard changeover function
        @param[in]     _rout: outer radius of soft-hard changeover function
        @param[in]     _dt_tree: tree time step for calculating r_search
        @param[in]     _id_offset: for artifical particles, the offset of starting id.
        @param[in]     _n_split: split number for artifical particles
     */
    void createFakeParticles(const PS::S32 _i_cluster,
                             Tptcl* _ptcl_in_cluster,
                             const PS::S32 _n_ptcl,
                             PS::ReallocatableArray<Tptcl> & _ptcl_artifical,
                             PS::S32 &_n_groups,
                             SearchGroup<Tptcl>& _groups,
                             const PS::F64 _rbin,
                             const PS::F64 _rin,
                             const PS::F64 _rout,
                             const PS::F64 _dt_tree,
                             const PS::S64 _id_offset,
                             const PS::S32 _n_split){
        if (_n_split>(1<<ID_PHASE_SHIFT)) {
            std::cerr<<"Error! ID_PHASE_SHIFT is too small for phase split! shift bit: "<<ID_PHASE_SHIFT<<" n_split: "<<_n_split<<std::endl;
            abort();
        }

        PS::S32 group_ptcl_adr_list[_n_ptcl];
        PS::S32 group_ptcl_adr_offset=0;
        _n_groups = 0;
        for (int i=0; i<_groups.getNumberOfGroups(); i++) {
            PS::ReallocatableArray<COMM::BinaryTree<Tptcl>> bins;   // hierarch binary tree
            bins.reserve(_n_groups);

            const PS::S32 n_members = _groups.getNumberOfGroupMembers(i);
#ifdef FAKE_PARTICLE_DEBUG
            assert(n_members<ARRAY_ALLOW_LIMIT);
#endif        
            PS::S32* member_list = _groups.getMemberList(i);
            bins.resizeNoInitialize(n_members-1);
            // build hierarch binary tree from the minimum distant neighbors

            COMM::BinaryTree<Tptcl>::generateBinaryTree(bins.getPointer(), member_list, n_members, _ptcl_in_cluster);

            // reset status to 0
            for (int j=0; j<n_members; j++) _ptcl_in_cluster[member_list[j]].status.d=0;

            struct {PS::F64 mean_mass_inv, rin, rout, dt_tree; } ch = {Tptcl::mean_mass_inv, _rin, _rout, _dt_tree};
            auto* chp = &ch;
            bins.back().processRootIter(chp, calcBinChangeOverAndRSearchIter);
            
            // stability check and break groups
            Stability<Tptcl> stab;
            stab.t_crit = _dt_tree*0.25;
            stab.stable_binary_tree.reserve(_n_groups);
            stab.findStableTree(bins.back());

            PS::ReallocatableArray<PS::S32> empty_list;
            for (int i=0; i<stab.stable_binary_tree.size(); i++) {
                keplerOrbitGenerator(_i_cluster, _n_groups, _ptcl_in_cluster, _ptcl_artifical, empty_list, &group_ptcl_adr_list[group_ptcl_adr_offset], *stab.stable_binary_tree[i], _rbin, _id_offset, _n_split);
                group_ptcl_adr_offset += stab.stable_binary_tree[i]->getMemberN();
                _n_groups++;
            }
        }

        assert(group_ptcl_adr_offset<=_n_ptcl);

        // Reorder the ptcl that group member come first
        PS::S32 ptcl_list_reorder[_n_ptcl];
        for (int i=0; i<_n_ptcl; i++) ptcl_list_reorder[i] = i;
 
        // shift single after group members
        PS::S32 i_single_front=group_ptcl_adr_offset;
        PS::S32 i_group = 0;
        while (i_group<group_ptcl_adr_offset) {
            // if single find inside group_ptcl_adr_offset, exchange single with group member out of the offset
            if(_ptcl_in_cluster[i_group].status.d==0) {
                while(_ptcl_in_cluster[i_single_front].status.d==0) {
                    i_single_front++;
                    assert(i_single_front<_n_ptcl);
                }
                // Swap index
                PS::S32 plist_tmp = ptcl_list_reorder[i_group];
                ptcl_list_reorder[i_group] = ptcl_list_reorder[i_single_front];
                ptcl_list_reorder[i_single_front] = plist_tmp;
                i_single_front++; // avoild same particle be replaced
            }
            i_group++;
        }

#ifdef FAKE_PARTICLE_DEBUG
        // check whether the list is correct
        PS::S32 plist_new[group_ptcl_adr_offset];
        for (int i=0; i<group_ptcl_adr_offset; i++) plist_new[i] = group_ptcl_adr_list[i];
        std::sort(plist_new, plist_new+group_ptcl_adr_offset, [](const PS::S32 &a, const PS::S32 &b) {return a < b;});
        std::sort(ptcl_list_reorder, ptcl_list_reorder+group_ptcl_adr_offset, [](const PS::S32 &a, const PS::S32 &b) {return a < b;});
        for (int i=0; i<group_ptcl_adr_offset; i++) assert(ptcl_list_reorder[i]==plist_new[i]);
#endif        

        // overwrite the new ptcl list for group members by reorderd list
        for (int i=0; i<group_ptcl_adr_offset; i++) ptcl_list_reorder[i] = group_ptcl_adr_list[i];

        // templately copy ptcl data
        Tptcl ptcl_tmp[_n_ptcl];
        for (int i=0; i<_n_ptcl; i++) ptcl_tmp[i]=_ptcl_in_cluster[i];

        // reorder ptcl
        for (int i=0; i<_n_ptcl; i++) _ptcl_in_cluster[i]=ptcl_tmp[ptcl_list_reorder[i]];

        //for (int i=0; i<_empty_list.size(); i++) {
        //    PS::S32 ik = _empty_list[i];
        //    _ptcl_in_cluster[ik].mass = 0.0;
        //    _ptcl_in_cluster[ik].id = -1;
        //    _ptcl_in_cluster[ik].status.d = -1;
        //}

    }

//    void checkRoutChange(PS::ReallocatableArray<RCList> & r_out_change_list,
//                         PS::ReallocatableArray<PS::ReallocatableArray<PS::S32>> & group_list,
//                         Tptcl* ptcl){
//        for (int i=0; i<group_list.size(); i++) {
//            PS::S64 r_out_max = 0.0;
//            for (int j=0; j<group_list[i].size(); i++) {
//                PS::S32 k = group_list[i][j];
//                r_out_max = std::max(r_out_max,ptcl[k].r_out);
//            }
//            for (int j=0; j<group_list[i].size(); i++) {
//                PS::S32 k = group_list[i][j];
//                if(ptcl[k].r_out != r_out_max) {
//                    r_out_change_list.push_back(RCList(k,ptcl[k].r_out));
//#ifdef FAKE_PARTICLE_DEBUG
//                    std::cerr<<"Rout change detected, p["<<k<<"].r_out: "<<ptcl[k].r_out<<" -> "<<r_out_max<<std::endl;
//#endif
//                    ptcl[k].r_out = r_out_max;
//                }
//            }
//        }
//    }


};

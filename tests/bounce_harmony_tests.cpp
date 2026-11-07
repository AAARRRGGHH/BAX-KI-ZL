#include "world.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace sm;using namespace sm::bounce;
std::array<int,3> reference_triad(const int* hits,size_t count,Selection scale,int low,int high) {
 std::array<int,3> best{{47,50,54}};
 if(!hits || !count || !valid(scale) || low<0 || high>8 || low>high)return best;
 const auto& melody=modes()[scale.mode].intervals;
 const int bottom=12*(low+1)+scale.root,top=std::min(127,12*(high+1)+scale.root+melody.back());
 const int basis=scale.mode==9?0:scale.mode==10 || scale.mode==11?1:scale.mode;
 const auto& intervals=modes()[basis].intervals;
 double best_score=1e30;
 for(int degree=0;degree<7;++degree){
   const int root=(scale.root+intervals[degree])%12,third=(scale.root+intervals[(degree+2)%7])%12,fifth=(scale.root+intervals[(degree+4)%7])%12;
   const auto in_scale=[&](int pc){return std::any_of(melody.begin(),melody.end(),[&](int n){return (scale.root+n)%12==pc;});};
   const std::array<int,3> quality{{0,(third-root+12)%12,(fifth-root+12)%12}};
   if((quality!=std::array<int,3>{{0,3,7}} && quality!=std::array<int,3>{{0,4,7}}) || !in_scale(root) || !in_scale(third) || !in_scale(fifth))continue;
   for(int a=bottom;a<=top;++a)if(a%12==root)
   for(int b=bottom;b<=top;++b)if(b%12==third)
   for(int c=bottom;c<=top;++c)if(c%12==fifth){
     std::array<int,3> chord{{a,b,c}};std::sort(chord.begin(),chord.end());
     // Symmetric pitch distance: keep the actual hits close to the chord,
     // and prevent the added chord tone from wandering away from the hits.
     double hit_distance=0,chord_distance=0,hit_center=0;
     for(size_t i=0;i<count;++i){int distance=128;for(int n:chord)distance=std::min(distance,std::abs(hits[i]-n));hit_distance+=distance*distance;hit_center+=hits[i];}
     for(int n:chord){int distance=128;for(size_t i=0;i<count;++i)distance=std::min(distance,std::abs(hits[i]-n));chord_distance+=distance*distance;}
     const double center=(a+b+c)/3.-hit_center/count;
     const double score=hit_distance/count+chord_distance/3.+.05*center*center;
     if(score<best_score-1e-9){best_score=score;best=chord;}
   }
 }
 return best;
}

int main(){
 std::mt19937 rng(41273);unsigned cases=0;
 for(int root=0;root<12;++root)for(int mode=0;mode<12;++mode)for(auto range:{std::array<int,2>{{2,5}},std::array<int,2>{{0,8}},std::array<int,2>{{8,8}}}){
   ChordSearch search;search.configure({root,mode},range[0],range[1]);
   for(int sample=0;sample<4;++sample){int hits[48]{};const size_t count=sample==3?48:size_t(sample*4+1);for(size_t i=0;i<count;++i)hits[i]=int(rng()%128);
     const auto expected=reference_triad(hits,count,{root,mode},range[0],range[1]);
     const auto actual=search.find(hits,count);
     if(actual!=expected){std::fprintf(stderr,"Harmony mismatch root=%d mode=%d low=%d high=%d sample=%d\n",root,mode,range[0],range[1],sample);return 1;}
     for(int pitch:actual){bool in_scale=false;for(int interval:modes()[mode].intervals)in_scale|=(root+interval)%12==pitch%12;if(!in_scale){std::fprintf(stderr,"Foreign pitch in harmony\n");return 1;}}
     bool stable=false;for(int r:actual){bool minor=false,major=false,fifth=false;for(int n:actual){minor|=(n-r+120)%12==3;major|=(n-r+120)%12==4;fifth|=(n-r+120)%12==7;}stable|=(minor || major) && fifth;}
     if(!stable){std::fprintf(stderr,"Unstable automatic triad\n");return 1;}++cases;
     const auto four=search.find_four(hits,count);int voices=0;bool classes[12]{};int distinct=0;
     for(int n:four)if(n>=0){if(n>127){std::fprintf(stderr,"Out-of-range fourth voice\n");return 1;}++voices;if(!classes[n%12]){classes[n%12]=true;++distinct;}bool allowed=false;for(int interval:modes()[mode].intervals)allowed|=(root+interval)%12==n%12;if(!allowed){std::fprintf(stderr,"Foreign fourth voice\n");return 1;}}
     if(distinct!=3 || voices<3 || (range[1]>range[0] && voices!=4)){std::fprintf(stderr,"Fourth voice must double a triad tone, not add a seventh\n");return 1;}
     if(voices==4){bool doubled_root=false;for(int r:four){int count_root=0;bool third=false,fifth=false;for(int n:four){count_root+=n%12==r%12;const int d=(n-r+120)%12;third|=d==3 || d==4;fifth|=d==7;}doubled_root|=count_root==2 && third && fifth;}if(!doubled_root){std::fprintf(stderr,"Fourth voice did not double the root\n");return 1;}}
   }
 }
 std::printf("Stable in-scale integer harmony matches floating-point reference: %u cases PASS\n",cases);
}

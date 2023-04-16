#include <set>
#include <map>
#include <iostream>
using namespace std;

int main(){
	int a[5] = {123,21,34,74,50};
	set<int * > tmp;
	map<int,int> tmp2;
	tmp.insert(a+3);
	tmp.insert(a+2);
	tmp.insert(a+3);
	tmp.insert(a+1);
	tmp.insert(a+4);
	for(auto it = tmp.begin(); it != tmp.end(); ++it){
		cout << *it << " " << **it << endl;
	}
	cout << endl;


}
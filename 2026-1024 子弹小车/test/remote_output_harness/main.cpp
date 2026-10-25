#include <mutex>
#include <string>
#include <cassert>
#define private public
#include "car_controller.h"
#undef private
int main() {
    CarController car;
    car.ApplyState(1); assert(car.state_==1);
    car.ApplyRemoteState(0); car.ApplyState(2); assert(car.state_==0);
    car.NextInput(); car.ApplyState(2); assert(car.state_==2);
    car.ApplyRemoteState(8); car.ApplyState(4); assert(car.state_==8);
    car.NextInput(); car.ApplyState(4); assert(car.state_==4);
    car.FireRemote(); assert(car.state_==0x14);
    car.FireRemote(); assert(car.state_==0x14);
    car.ApplyState(2); assert(car.state_==0x14);
    car.ApplyRemoteState(0); assert(car.state_==0);
    car.FireRemote(); assert(car.state_==0x10);
    car.NextInput(); car.ApplyState(0); assert(car.state_==0);
}

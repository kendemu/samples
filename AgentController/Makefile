#sigverse header
SIG_SRC  = $(SIGVERSE_PATH)/include/sigverse

OBJS     = AgentController.so AgentController2.so

all: $(OBJS)

#compile
./%.so: ./%.cpp
	g++ -Wall -DCONTROLLER -DNDEBUG -DUSE_ODE -DdDOUBLE -I$(SIG_SRC) -I$(SIG_SRC)/comm/controller -fPIC -shared -o $@ $<

clean:
	rm ./*.so *~

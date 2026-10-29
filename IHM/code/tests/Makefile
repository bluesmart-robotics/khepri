P := ../components/khepri_proto
all: test_proto test_knob
test_proto: test_proto.c $(P)/src/khepri_espnow.c $(P)/src/kh_model.c
	$(CC) -std=c11 -Wall -Wextra -Werror -O1 -I$(P)/include -o $@ $^ -lm
test_knob: test_knob.c ../main/knob.c
	$(CC) -std=c11 -Wall -Wextra -Werror -O1 -I../main -o $@ $^ -lm
clean:
	rm -f test_proto test_knob

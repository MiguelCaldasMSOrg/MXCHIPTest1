#pragma once

class IRDASensor {
  public:
  int init();
  unsigned char IRDATransmit(unsigned char *data, int size, int timeout);
};

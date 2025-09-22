typedef unsigned int uint32_t;

enum MaskingOperator {
  eMaskingOperatorDefault = 0,
  eMaskingOperatorAnd = 1,
  eMaskingOperatorOr = 2,
  eMaskingOperatorXor = 3,
  eMaskingOperatorNand = 4,
  eMaskingOperatorNor = 5
};

class MaskedData {
private:
  uint32_t value;
  uint32_t mask;
  MaskingOperator oper;

public:
  MaskedData(uint32_t V = 0, uint32_t M = 0,
             MaskingOperator P = eMaskingOperatorDefault)
      : value(V), mask(M), oper(P) {}

  uint32_t apply() {
    switch (oper) {
    case eMaskingOperatorAnd:
      return value & mask;
    case eMaskingOperatorOr:
      return value | mask;
    case eMaskingOperatorXor:
      return value ^ mask;
    case eMaskingOperatorNand:
      return ~(value & mask);
    case eMaskingOperatorNor:
      return ~(value | mask);
    case eMaskingOperatorDefault: // fall through
    default:
      return value;
    }
  }

  void setValue(uint32_t V) { value = V; }

  void setMask(uint32_t M) { mask = M; }

  void setOperator(MaskingOperator P) { oper = P; }
};

int main() {
  MaskedData data_1(0xFF0F, 0xA01F, eMaskingOperatorAnd);
  MaskedData data_2(data_1.apply(), 0x1AFC, eMaskingOperatorXor);
  MaskedData data_3(data_2.apply(), 0xFFCF, eMaskingOperatorOr);
  MaskedData data_4(data_3.apply(), 0xAABC, eMaskingOperatorAnd);
  MaskedData data_5(data_4.apply(), 0xFFAC, eMaskingOperatorNor);
  MaskedData data_6(data_5.apply(), 0x0000BEEF, eMaskingOperatorAnd);
  return data_6.apply(); // <-- what comes out of here?
}

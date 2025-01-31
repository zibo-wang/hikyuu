/*
 *  Copyright (c) 2025 hikyuu.org
 *
 *  Created on: 2025-01-31
 *      Author: fasiondog
 */

#pragma once
#ifndef INDICATOR_IMP_ICONST_H_
#define INDICATOR_IMP_ICONST_H_

#include "../Indicator.h"

namespace hku {

class IConst : public IndicatorImp {
    INDICATOR_IMP(IConst)
    INDICATOR_IMP_NO_PRIVATE_MEMBER_SERIALIZATION

public:
    IConst();
    virtual ~IConst();
};

} /* namespace hku */
#endif /* INDICATOR_IMP_ICONST_H_ */

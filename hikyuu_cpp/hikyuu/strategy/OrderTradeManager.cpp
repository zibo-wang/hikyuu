/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-08-16
 *      Author: fasiondog
 */

#include "hikyuu/trade_manage/crt/TC_Zero.h"
#include "OrderTradeManager.h"

namespace hku {

OrderTradeManager::OrderTradeManager(const Datetime& datetime, price_t initcash,
                                     const TradeCostPtr& costfunc, const string& name)
: TradeManagerBase(name, costfunc),
  m_init_datetime(datetime),
  m_first_datetime(datetime),
  m_last_datetime(datetime) {
    m_init_cash = roundEx(initcash, 2);
    m_cash = m_init_cash;
    m_broker_last_datetime = Datetime::now();
}

void OrderTradeManager::_reset() {
    HKU_WARN("The subclass does not implement a reset method");
    m_first_datetime = m_init_datetime;
    m_last_datetime = m_init_datetime;
    m_cash = m_init_cash;
    m_frozen_cash = 0.0;
    m_position.clear();
}

shared_ptr<TradeManagerBase> OrderTradeManager::_clone() {
    OrderTradeManager* p = new OrderTradeManager(m_init_datetime, m_init_cash, m_costfunc, m_name);
    p->m_init_datetime = m_init_datetime;
    p->m_first_datetime = m_first_datetime;
    p->m_last_datetime = m_last_datetime;
    p->m_init_cash = m_init_cash;
    p->m_cash = m_cash;
    p->m_frozen_cash = m_frozen_cash;
    p->m_position = m_position;
    return shared_ptr<TradeManagerBase>(p);
}

PositionRecordList OrderTradeManager::getPositionList() const {
    PositionRecordList result;
    position_map_type::const_iterator iter = m_position.begin();
    for (; iter != m_position.end(); ++iter) {
        result.push_back(iter->second);
    }
    return result;
}

bool OrderTradeManager::checkin(const Datetime& datetime, price_t cash) {
    HKU_IF_RETURN(datetime < m_last_datetime, false);
    m_cash += cash;
    return true;
}

TradeRecord OrderTradeManager::buy(const Datetime& datetime, const Stock& stock, price_t realPrice,
                                   double number, price_t stoploss, price_t goalPrice,
                                   price_t planPrice, SystemPart from) {
    TradeRecord result;
    result.business = BUSINESS_INVALID;

    HKU_ERROR_IF_RETURN(stock.isNull(), result, "{} Stock is Null!", datetime);
    HKU_ERROR_IF_RETURN(datetime < lastDatetime(), result,
                        "{} {} datetime must be >= lastDatetime({})!", datetime,
                        stock.market_code(), lastDatetime());
    HKU_ERROR_IF_RETURN(number == 0.0, result, "{} {} numer is zero!", datetime,
                        stock.market_code());
    HKU_ERROR_IF_RETURN(number < stock.minTradeNumber(), result,
                        "{} {} Buy number({}) must be >= minTradeNumber({})!", datetime,
                        stock.market_code(), number, stock.minTradeNumber());
    HKU_ERROR_IF_RETURN(number > stock.maxTradeNumber(), result,
                        "{} {} Buy number({}) must be <= maxTradeNumber({})!", datetime,
                        stock.market_code(), number, stock.maxTradeNumber());

    CostRecord cost = getBuyCost(datetime, stock, realPrice, number);

    // 实际交易需要的现金＝交易数量＊实际交易价格＋交易总成本
    int precision = getParam<int>("precision");
    // price_t money = roundEx(realPrice * number * stock.unit() + cost.total, precision);
    price_t money = roundEx(realPrice * number * stock.unit(), precision);

    HKU_WARN_IF_RETURN(m_cash < roundEx(money + cost.total, precision), result,
                       "{} {} Can't buy, need cash({:<.4f}) > current cash({:<.4f})!", datetime,
                       stock.market_code(), roundEx(money + cost.total, precision), m_cash);

    // 更新现金
    m_cash = roundEx(m_cash - money - cost.total, precision);

    // 加入交易记录
    result = TradeRecord(stock, datetime, BUSINESS_BUY, planPrice, realPrice, goalPrice, number,
                         cost, stoploss, m_cash, from);

    // 更新当前持仓记录
    position_map_type::iterator pos_iter = m_position.find(stock.id());
    if (pos_iter == m_position.end()) {
        m_position[stock.id()] = PositionRecord(
          stock, datetime, Null<Datetime>(), number, stoploss, goalPrice, number, money, cost.total,
          roundEx((realPrice - stoploss) * number * stock.unit(), precision), 0.0);
    } else {
        PositionRecord& position = pos_iter->second;
        position.number += number;
        position.stoploss = stoploss;
        position.goalPrice = goalPrice;
        position.totalNumber += number;
        position.buyMoney = roundEx(money + position.buyMoney, precision);
        position.totalCost = roundEx(cost.total + position.totalCost, precision);
        position.totalRisk =
          roundEx(position.totalRisk + (realPrice - stoploss) * number * stock.unit(), precision);
    }

    if (datetime > m_broker_last_datetime) {
        list<OrderBrokerPtr>::const_iterator broker_iter = m_broker_list.begin();
        string broker_ret;
        for (; broker_iter != m_broker_list.end(); ++broker_iter) {
            broker_ret =
              (*broker_iter)->buy(datetime, stock.market(), stock.code(), realPrice, number);
            if (!broker_ret.empty() && datetime > m_broker_last_datetime) {
                m_broker_last_datetime = datetime;
            }
        }
    }

    return result;
}

TradeRecord OrderTradeManager::sell(const Datetime& datetime, const Stock& stock, price_t realPrice,
                                    double number, price_t stoploss, price_t goalPrice,
                                    price_t planPrice, SystemPart from) {
    HKU_CHECK(!std::isnan(number), "sell number should be a valid double!");
    TradeRecord result;

    HKU_ERROR_IF_RETURN(stock.isNull(), result, "{} Stock is Null!", datetime);
    HKU_ERROR_IF_RETURN(datetime < lastDatetime(), result,
                        "{} {} datetime must be >= lastDatetime({})!", datetime,
                        stock.market_code(), lastDatetime());
    HKU_ERROR_IF_RETURN(number == 0.0, result, "{} {} number is zero!", datetime,
                        stock.market_code());

    // 对于分红扩股造成不满足最小交易量整数倍的情况，只能通过number=MAX_DOUBLE的方式全仓卖出
    HKU_ERROR_IF_RETURN(number < stock.minTradeNumber(), result,
                        "{} {} Sell number({}) must be >= minTradeNumber({})!", datetime,
                        stock.market_code(), number, stock.minTradeNumber());
    HKU_ERROR_IF_RETURN(number != MAX_DOUBLE && number > stock.maxTradeNumber(), result,
                        "{} {} Sell number({}) must be <= maxTradeNumber({})!", datetime,
                        stock.market_code(), number, stock.maxTradeNumber());

    // 未持仓
    position_map_type::iterator pos_iter = m_position.find(stock.id());
    HKU_TRACE_IF_RETURN(pos_iter == m_position.end(), result,
                        "{} {} This stock was not bought never! ({}, {:<.4f}, {}, {})", datetime,
                        stock.market_code(), datetime, realPrice, number, getSystemPartName(from));

    PositionRecord& position = pos_iter->second;

    // 调整欲卖出的数量，如果卖出数量等于MAX_DOUBLE，则表示卖出全部
    double real_number = number == MAX_DOUBLE ? position.number : number;

    // 欲卖出的数量大于当前持仓的数量
    HKU_ERROR_IF_RETURN(position.number < real_number, result,
                        "{} {} Try to sell number({}) > number of position({})!", datetime,
                        stock.market_code(), real_number, position.number);

    CostRecord cost = getSellCost(datetime, stock, realPrice, real_number);

    int precision = getParam<int>("precision");
    price_t money = roundEx(realPrice * real_number * stock.unit(), precision);

    // 更新现金余额
    m_cash = roundEx(m_cash + money - cost.total, precision);

    // 更新交易记录
    result = TradeRecord(stock, datetime, BUSINESS_SELL, planPrice, realPrice, goalPrice,
                         real_number, cost, stoploss, m_cash, from);

    // 更新当前持仓情况
    position.number -= real_number;
    position.stoploss = stoploss;
    position.goalPrice = goalPrice;
    // position.buyMoney = position.buyMoney;
    position.totalCost = roundEx(position.totalCost + cost.total, precision);
    position.sellMoney = roundEx(position.sellMoney + money, precision);

    if (position.number == 0) {
        // 删除当前持仓
        m_position.erase(stock.id());
    }

    if (datetime > m_broker_last_datetime) {
        list<OrderBrokerPtr>::const_iterator broker_iter = m_broker_list.begin();
        string broker_ret;
        for (; broker_iter != m_broker_list.end(); ++broker_iter) {
            broker_ret =
              (*broker_iter)->sell(datetime, stock.market(), stock.code(), realPrice, real_number);
            if (!broker_ret.empty() && datetime > m_broker_last_datetime) {
                m_broker_last_datetime = datetime;
            }
        }
    }

    return result;
}

}  // namespace hku
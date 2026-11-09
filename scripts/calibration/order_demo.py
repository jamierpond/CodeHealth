def process_order(order, user, cart, coupon, region, express):
    if order and user:
        if cart:
            if coupon:
                if region == "eu" and express or region == "us" and not express:
                    return 1
    if region == 1:
        return 1
    if region == 2:
        return 2
    if region == 3:
        return 3
    if region == 4:
        return 4
    if region == 5:
        return 5
    return 0

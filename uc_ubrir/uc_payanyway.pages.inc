<?php
/**
 * @file
 * payanyway menu items.
 *
 */

function uc_payanyway_callback() {
    watchdog('uc_payanyway', 'Receiving new order notification for order !order_id.', array('!order_id' => check_plain($_REQUEST['MNT_TRANSACTION_ID'])));

	if(isset($_REQUEST['MNT_ID']) && isset($_REQUEST['MNT_TRANSACTION_ID']) && isset($_REQUEST['MNT_OPERATION_ID'])
	   && isset($_REQUEST['MNT_AMOUNT']) && isset($_REQUEST['MNT_CURRENCY_CODE']) && isset($_REQUEST['MNT_TEST_MODE'])
	   && isset($_REQUEST['MNT_SIGNATURE']) && isset($_REQUEST['cart_id']))
	{
        $mnt_dataintegrity_code = variable_get('uc_payanyway_mnt_dataintegrity_code', '');
        $mnt_signature = md5("{$_REQUEST['MNT_ID']}{$_REQUEST['MNT_TRANSACTION_ID']}{$_REQUEST['MNT_OPERATION_ID']}{$_REQUEST['MNT_AMOUNT']}{$_REQUEST['MNT_CURRENCY_CODE']}{$_REQUEST['MNT_TEST_MODE']}{$mnt_dataintegrity_code}");
        if ($_REQUEST['MNT_SIGNATURE'] == $mnt_signature)
        {
            $order = uc_order_load($_REQUEST['MNT_TRANSACTION_ID']);

            uc_cart_empty($_REQUEST['cart_id']);

            // Save changes to order without it's completion.
            $order->order_status = 'payment_received';
            uc_order_save($order);
			uc_payment_enter($_REQUEST['MNT_TRANSACTION_ID'], 'payanyway', $_REQUEST['MNT_AMOUNT'], $order->uid, NULL, $description);
            uc_order_comment_save($order->order_id, 0, t('Payment received.'), 'admin');
            watchdog('uc_payanyway', 'Receiving new order notification for order !order_id. Payment completed!', array('!order_id' => check_plain($_REQUEST['MNT_TRANSACTION_ID'])));
            echo 'SUCCESS';
        }
        else {
            watchdog('uc_payanyway', 'Receiving new order notification for order !order_id. Wrong signature!', array('!order_id' => check_plain($_REQUEST['MNT_TRANSACTION_ID'])));
            echo 'FAIL';
        }
    }
    else {
        watchdog('uc_payanyway', 'Receiving new order notification for order !order_id. Bad request!', array('!order_id' => check_plain($_REQUEST['MNT_TRANSACTION_ID'])));
        echo 'FAIL';
    }
}

function uc_payanyway_payment_end($type) {
//var_dump($output); exit;
  if ($type=='success') {
    if (isset($_SESSION['cart_order'])) {
      $order_id=$_SESSION['cart_order'];
      $_SESSION['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
    }
  }
  else {
    unset($_SESSION['cart_order']);
    drupal_set_message( t("Your payment has been cancelled."));
    drupal_goto('cart');
  }
  return $output;
}

function uc_payanyway_invoice() {
	$payanyway_server = variable_get('uc_payanyway_server', '');
	$login = variable_get('uc_payanyway_login', '');
	$password = variable_get('uc_payanyway_password', '');

	require_once (dirname(__FILE__).'/MonetaAPI/MonetaWebService.php');
	switch ($payanyway_server) {
		case 'www.payanyway.ru':
			$service = new MonetaWebService("https://www.moneta.ru/services.wsdl", $login, $password);
			break;
		case 'demo.moneta.ru':
			$service = new MonetaWebService("https://demo.moneta.ru/services.wsdl", $login, $password);
			break;
	}

	try
	{
		// запрос стоимости и комиссии
		$transactionRequestType = new MonetaForecastTransactionRequest();
		$transactionRequestType->payer = $_REQUEST['paymentSystem_accountId'];
		$transactionRequestType->payee = $_REQUEST['MNT_ID'];
		$transactionRequestType->amount = $_REQUEST['MNT_AMOUNT'];
		$transactionRequestType->clientTransaction = $_REQUEST['MNT_TRANSACTION_ID'];
		$forecast = $service->ForecastTransaction($transactionRequestType);

		// получить данные счета
		$request = new MonetaInvoiceRequest();
		$request->payer = $_REQUEST['paymentSystem_accountId'];
		$request->payee = $_REQUEST['MNT_ID'];
		$request->amount = $_REQUEST['MNT_AMOUNT'];
		$request->clientTransaction = $_REQUEST['MNT_TRANSACTION_ID'];

		if ($_REQUEST['payment_method'] == 'postal')
		{
			$operationInfo = new MonetaOperationInfo();
			$a1 = new MonetaKeyValueAttribute();
			$a1->key = 'mailofrussiaindex';
			$a1->value = $_REQUEST['additionalParameters_mailofrussiaSenderIndex'];
			$operationInfo->addAttribute($a1);
			$a2 = new MonetaKeyValueAttribute();
			$a2->key = 'mailofrussiaaddress';
			$a2->value = $_REQUEST['additionalParameters_mailofrussiaSenderAddress'];
			$operationInfo->addAttribute($a2);
			$a3 = new MonetaKeyValueAttribute();
			$a3->key = 'mailofrussianame';
			$a3->value = $_REQUEST['additionalParameters_mailofrussiaSenderName'];
			$operationInfo->addAttribute($a3);
			$request->operationInfo = $operationInfo;
		}
		elseif ($_REQUEST['payment_method'] == 'euroset')
		{
			$operationInfo = new MonetaOperationInfo();
			$a1 = new MonetaKeyValueAttribute();
			$a1->key = 'rapidamphone';
			$a1->value = $_REQUEST['additionalParameters_rapidaPhone'];
			$operationInfo->addAttribute($a1);
			$request->operationInfo = $operationInfo;
		}
		$response = $service->Invoice($request);
		if ($_REQUEST['payment_method'] == 'euroset')
		{
			$response1 = $service->GetOperationDetailsById($response->transaction);
			foreach ($response1->operation->attribute as $attr)
			{
				if ($attr->key == 'rapidatid')
				{
					$transaction_id = $attr->value;
				}
			}
		}
		else
		{
			$transaction_id = $response->transaction;//(!empty($response->transaction))?$response->transaction:$response->clientTransaction;
		}

		$invoice->status = $response->status;
		$invoice->transaction = str_pad($transaction_id, 9, "0", STR_PAD_LEFT);
		$invoice->system = $_REQUEST['payment_method'];
		$invoice->amount = $_REQUEST['MNT_AMOUNT']." ".$_REQUEST['MNT_CURRENCY_CODE'];
		$invoice->payerAmount = number_format($forecast->payerAmount, 2, '.', '')." ".$forecast->payerCurrency;
		$invoice->payerFee = number_format($forecast->payerFee, 2, '.', '');
		uc_cart_empty($_REQUEST['cart_id']);
	}
	catch (Exception $e)
	{
		$invoice->status = 'FAILED';
		$invoice->error_message = $e->getMessage();
	}
	$output = theme('uc_payanyway_invoice_print', $invoice);
	return $output;
}

function theme_uc_payanyway_invoice_print($invoice)
{
	if ($invoice->status == 'CREATED')
	{
		$output .= "<h3>".t('Invoice was created')."</h3>";
		switch($invoice->system) {
			case 'postal':
				$output .= "<p>".t("Transaction is registered for processing. To complete your payment please <a target='_blank' href='https://www.payanyway.ru/mailofrussiablank.htm?operationId=!transaction'>print post ticket</a> and proceed with payment in any <a target='_blank' href='http://www.russianpost.ru'>Russian Post</a> post office.To view post ticket in PDF format you should have a program <a target='_blank' href='http://get.adobe.com/reader/'>Adobe Acrobat Reader</a> installed.", 
									array('!transaction'=>$invoice->transaction))."</p>";
				break;
			case 'banktransfer':
				$output .= "<p>".t("Transaction is registered for processing. To complete your payment please <a target='_blank' href='https://www.payanyway.ru/wiretransferreceipt.htm?transactionId=!transaction'>print receipt</a> and make payment in bank office.", 
									array('!transaction'=>$invoice->transaction))."</p>";
				break;
			case 'forward':
				$output .= "<p>".t("Contract number for Forward Mobile is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with Forward Mobile system using following <b>MONETA.RU</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'ciberpay':
				$output .= "<p>".t("Contract number for CiberPay is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with CiberPay system using following <b>MONETA.RU</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'novoplat':
				$output .= "<p>".t("Contract number for NovoPlat is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with NovoPlat system using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'platika':
				$output .= "<p>".t("Contract number for PLATiKA is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with PLATiKA system using following <b>MONETA.RU</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'mcb':
				$output .= "<p>".t("Contract number for MosCreditBank is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with MosCreditBank cash payment terminals using following <b>MONETA.RU</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'elecsnet':
				$output .= "<p>".t("Contract number for Elecsnet is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with Elecsnet cash payment terminals using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'comepay':
				$output .= "<p>".t("Contract number for ComePay is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with ComePay cash payment terminals using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'gorod':
				$output .= "<p>".t("Contract number for Federal system GOROD is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with Federal system GOROD using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'contact':
				$output .= "<p>".t("Contract number for Contact is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with Contact payment system using following <b>MONETA.RU</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'euroset':
				$output .= "<p>".t("Contract number for Euroset is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment with Euroset using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'megafon':
				$output .= "<p>".t("The identifier for payment is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Megafon mobile payment service is an easy and secure way to make istant payments.")."</p>";
				$output .= "<p>".t("Beware!<br/>· Colons between SMS text parts are required<br/>· There must be not less than 10 RUB on your mobile phone account after transaction complete.")."</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed with payment with Soyuztelecom sending following SMS using Megafon cellular network provider (in Russia only) to short number <b>843808</b>:")."</p>";
				$output .= "<p>".t("<b>503</b>[colon]<b>IDENTIFIER</b>")."</p>";
				$output .= "<p>".t("In this case: 503:{$invoice->transaction}")."</p>";
				$output .= "<p>".t("Then you will receive success SMS-notice.")."</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'mts':
				$output .= "<p>".t("MTS mobile payment service is an easy and secure way to make instant payments.")."</p>";
				$output .= "<p>".t("Transaction is registered.")."</p>";
				$output .= "<p>".t("You will receive SMS with operation acknowledge request. Answer with digit 1 as SMS text if you agree. You will receive success SMS-notice after sending acknowledge.")."</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'kassiranet':
				$output .= "<p>".t("Contract number is:")." {$invoice->transaction}</p>";
				$output .= "<p>".t("Transaction is registered. Please proceed payment using following <b>PayAnyWay</b> account number:")."</p>";
				$output .= "<p>{$invoice->transaction}</p>";
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
			case 'postonline':
				$output .= "<p>".t("An operation has been created but not completed. To complete a payment please do the following:");
				$output .= "<ol>";
				$output .= "<li>".t("Come up to the cashier in any office 'Russian Post', at the desk 'Payments'.")."</li>";
				$output .= "<li>".t("Say that you would like to make a payment in favour of PayAnyWay (section 'Electronic money').")."</li>";
				$output .= "<li>".t("Say the payment account number, received at PayAnyWay website: <b>!transaction</b>.",array('!transaction'=>$invoice->transaction))."</li>";
				$output .= "<li>".t("After the cashier confirms the account number, say the amount of your payment.")."</li>";
				$output .= "<li>".t("When the amount is confirmed by the cashier, complete the payment.")."</li>";
				$output .= "</ol>";
				$output .= "<p>".t("Be sure to keep the receipt, please! Deposit timeline is 1 minute.")."</p>";
				$output .= "<p>".t("Payment account number:")." {$invoice->transaction}</p>";
				
				$output .= "<p>".t("Amount:")." {$invoice->amount}</p>";
				$output .= "<p>".t("External commission:")." {$invoice->payerFee}</p>";
				$output .= "<p>".t("Total amount:")." {$invoice->payerAmount}</p>";
				break;
		}
	}
	else
	{
		$output .= "<h3>".t('Error occured during creating invoice')."</h3>";
		$output .= "<p>".$invoice->error_message."</p>";
	}
	return $output;
}
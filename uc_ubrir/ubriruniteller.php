<?php
 
define('DRUPAL_ROOT', dirname(__FILE__).'/../../../../../../');
chdir(DRUPAL_ROOT);
require './includes/bootstrap.inc';
drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);


if (isset($_POST['SIGN'])) {
				$sign = strtoupper(md5(md5($_POST['SHOP_ID']).'&'.md5($_POST["ORDER_ID"]).'&'.md5($_POST['STATE'])));
				if ($_POST['SIGN'] == $sign) {
					switch ($_POST['STATE']) {
						case 'paid':
						  $order = uc_order_load($_POST["ORDER_ID"]);
						  uc_payment_enter($_POST["ORDER_ID"], 'uc_ubrir', $order->order_total, 0, NULL, $_POST["ORDER_ID"]);
						  uc_cart_complete_sale($order, variable_get('uc_new_customer_login', FALSE));
	 					  break;
					  }
			    }
			}  


?>